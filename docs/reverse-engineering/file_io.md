# File I/O of DBSIM and VSHELL

Every call site of a file-related import in the reference `VER95/DBSIM.EXE`
and `VER95/VSHELL.EXE` ([reference_version.md](../reference_version.md)): the function that owns it, its
caller chain up to a known subsystem, the path, read or write, the phase, and
the own code that covers the file. Then the contracts of the two hook groups
built on it: DBSIM's VOL-aware read stream (group `vfs`) and its configuration
and tape-header functions (group `config`). Static findings come from the
`llvm-objdump` listing; CW3220.DLL (CD root, hash in
`data/reference_hashes.txt`) was read for the `_open` flag semantics.
How the sites were collected: [Evidence](#evidence).

## Summary

- **Two process-private I/O stacks with the same design.** Both executables
  link the same small stream library: a VOL-aware read stream (loose file
  first, then the registered archives), a read-only file stream, a
  truncating write stream and a read/write stream that does **not**
  truncate. Resources go through the VOL-aware stream; launch files, saves,
  preferences and results through the plain file streams; a handful of
  configuration and debug files through C `fopen`.
- **All paths are relative.** Both programs read `data\drive.cfg` from the
  current directory, keep the installation root as an empty string, take the
  CD root from the first line of `drive.cfg`, and use the CD root only for
  the second VOL enumeration pass, the help file, one DBSIM voice path
  (`0x436982`) and the VSHELL intro movie.
- **Writes** (what a VFS must allow): DBSIM writes `data\results.dat`,
  `data\mission.var`, `data\prefs.cfg` (or `tapes\prefs.cfg`), `assert.log`
  and, only with tape options, `<name>.tap`, `<name>.dmp` and restored
  `data\…`/`tapes\…` files; it deletes `tapes\prefs.cfg` and
  `tapes\keyjoy.cfg` with `OpenFile(OF_DELETE)`. VSHELL writes the four
  launch files, `data\prefs.cfg`, `sav\gamefile.str`, `sav\GAME_n.SAV`, the
  `sav\script%d.dat`/`missn%d.str`/`player%d.mec` copies (and back), and
  `assert.log`; it deletes `data\player.mec` before rewriting it.
- **Non-truncating rewrites (static, CW3220 code read).** VSHELL
  rewrites `data\script.dat`, `data\mission.str`, `sav\GAME_n.SAV` and
  `sav\gamefile.str` through the read/write stream, which opens with
  `OPEN_ALWAYS` and never truncates. A shorter new file therefore keeps the
  old file's tail. The trailing bytes of `GAMEFILE.STR` (4) and `GAME_R.SAV`
  (10) are such stale tails ([mission.md](../formats/mission.md#sav-write-sequence-and-stale-tail)).
- **Tapes.** `-r`/`-p`/`-D` record and play back `.tap` files: a header of
  seven embedded launch/config files, then one appended record per frame
  ([DBSIM recording and playback](#dbsim-recording-and-playback)). The
  stage-6 callback `0x401e5c` belongs to the separate `-d` "dmp" option; the
  function that would fill its buffer (`0x401dc0`) has no static reference.
- **BND:** no file call site in either executable builds or opens a `.bnd`
  path ([BND files](#bnd-files)).

## Method and limits

1. Import tables and IAT thunks from the PE headers; direct callers by a byte
   scan for `FF 25` thunks and `E8`/`E9` calls to them (no `FF 15` calls
   exist). Output: `dbsim-import-callers.txt`, `vshell-import-callers.txt`.
2. Disassembly with LLVM `llvm-objdump -d -M intel` of the CD files
   (`dbsim-disassembly.txt`, `vshell-disassembly.txt`).
3. Function entries approximated as direct `call` targets, code addresses
   pushed or moved as immediates, and DATA dwords pointing at a push
   instruction (vtable slots). The owner of a site is the nearest entry below
   it; every owner named in this document was checked by reading its
   prologue. Callers are direct calls and pointer immediates; vtable calls
   are not followed (`tools\who.py`, `*-chains.txt`).
4. Paths from string immediates in the owner (`tools\show.py`,
   `tools\call_args.py`) and a scan of all path-like DATA strings with
   their references (`*-path-strings.txt`).
5. "No static reference" means: no `E8`/`E9`/`0F 8x` branch to the address
   anywhere in CODE, no absolute dword equal to it anywhere in the file, and
   no immediate. Computed targets are not excluded.

Phases: **startup** (WinMain up to the main loop or the shell's first
screen), **mission load** (DBSIM `0x461608`), **frame** (inside the DBSIM
main loop), **mission end** (DBSIM `0x461ff0` and the tail of `0x45f160`),
**shell** (VSHELL screens: game load/save, preparation, results), **error**
(assert/error paths), **debug** (code that needs a debug option or has no
static reference).

## File-related imports

| Import (DLL) | DBSIM sites | VSHELL sites | Use |
|---|---:|---:|---|
| `_fopen` `_fclose` `_fread` `_fwrite` `_fseek` `_ftell` `_rewind` `_fscanf` (CW3220) | 16, 20, 14, 11, 1, 1, 1, 4 | 5, 2, 0, 0, 0, 0, 0, 4 | config files, tapes, text grid |
| `_open` `_close` `_read` `_write` `_lseek` `_filelength` | 5, 5, 3, 5, 19, 1 | 5, 5, 3, 5, 19, 1 | stream library, `assert.log` |
| `_findfirst` `_findnext` | 2, 2 | 2, 2 | VOL enumeration |
| `_remove` | — | 1 | `data\player.mec` |
| `_fputs` | 1 | 1 | assert message to `stderr` |
| `_flushall` | 1 | 1 | RTL exit path |
| `_scanf` | — | 1 | AVI player ([VSHELL call sites](#vshell-call-sites)) |
| `OpenFile` (KERNEL32) | 2 | — | delete two tape-mode files |
| `_lopen` `_llseek` `_hread` `_lclose` | 2, 2, 3, 2 | 2, 2, 3, 2 | audio sample/music loads |
| `GetPrivateProfileStringA` | 8 | 4 | `sound.cfg`, `keyjoy.cfg` |
| `CreateFileA` `CloseHandle` `DeviceIoControl` | 1, 2, 6 | 1, 2, 6 | `\\.\DARKMONO.VXD` debug monitor |
| `LoadLibraryA` `FreeLibrary` | 3, 1 | 3, 1 | `BWCC32.DLL`/`BWCC.DLL`, SOS DLLs |
| `mciSendCommandA` / `mciSendStringA` | 17 / — | 10 / 7 | CD audio, AVI |
| `WinHelpA` | 1 | 1 | `es2guide.hlp` |
| `_printf` `__fputc` | 51, 54 | 69, 54 | `stdout` debug dumps, usage text |

Totals of genuine file call sites: DBSIM 112 RTL + 22 Win32, VSHELL 57 RTL
+ 16 Win32. `__streams` (`stdin`/`stdout`/`stderr`) is only referenced as a
data address: `stdout` at `__streams + 0x18` and `stderr` at `+ 0x30` (Borland
`FILE` size 0x18).

**`_open` flags (static, CW3220 `_open`, export 155 →
`0x409d84`):** access is `flags & 3` (0 read, 1 write, 2 read/write);
`flags & 0x700` selects the creation disposition: `0x100` `OPEN_ALWAYS`,
`0x200` `TRUNCATE_EXISTING`, `0x300` `CREATE_ALWAYS`, `0x500`/`0x700`
`CREATE_NEW`, otherwise `OPEN_EXISTING`; `0x800` is append, `0x8000` binary.
The flags used by the games therefore mean:

| Flags | Meaning | Used by |
|---|---|---|
| `0x8000` | read, open existing | VOL-aware stream, read stream |
| `0x8301`, mode `0x180` | write, create always (truncate) | write stream |
| `0x8102`, mode `0x180` | read/write, open always, **no truncation** | read/write stream |
| `0x0901`, mode `0x180` | write, open always, append (text) | `assert.log` |

Neither executable imports a truncation function (`chsize`/`ftruncate`) or
`SetEndOfFile`.

## The stream library and path helpers

| Role | DBSIM | VSHELL |
|---|---|---|
| VOL-aware read stream: ctor+open / open | `0x472f14` (vtable `0x4a0e10`) / `0x4730e8` (`_open` `0x47311c`, fallback `0x468064`) | `0x403065` / `0x4032b1` (`_open` `0x4032e5`, fallback `0x4040e9`) |
| … read / size / seek / close | `0x4735b8` (`_read`), `0x473334` (`_filelength`, `_lseek`), `0x472e6c`, `0x472f64` (dtor), `0x47307c` (`_close`) | `0x403985`, `0x4036a3`, `0x4036d4`/`0x40370b`, `0x4030e4`/`0x402f24`, `0x40323e` |
| Read stream (`0x8000`): ctor / open / read / close | `0x47ad40` (vtable `0x4a1738`) / `0x47ae68` / `0x47af98` / `0x47ae28` | `0x44ed84` (vtable `0x47a730`) / `0x44eeac` / `0x44efdc` / `0x44ee6c` |
| Write stream (`0x8301`): ctor / open / write / close | `0x47b064` (vtable `0x4a1760`) / `0x47b198` / `0x47b2b8` / `0x47b158` | `0x44f100` (vtable `0x47a758`) / `0x44f234` / `0x44f354` / `0x44f1f4` |
| Read/write stream (`0x8102`): ctor / open / I/O / close | `0x47a998` (vtable `0x4a1710`) / `0x47aabc` / `0x47abb8` / `0x47aa7c` | `0x44e9d8`, default ctor `0x44e95c` (vtable `0x47a708`) / `0x44eafc` / `0x44ebf8` / `0x44eabc` |
| VOL registration (`_findfirst`/`_findnext`) | `0x473260` → `0x467cd8` | `0x40356f` → `0x403caf` |
| Archive open (through the read stream) | `0x47cab0` (`0x47caf0`) | `0x4027b2` (`0x4027f3`) |
| Archive member lookup | `0x47cdf8` | `0x402d2b` |
| Assert log (`_open`/`_write`/`_close`) | `0x479e8c`, path builder `0x479e48`, root setter `0x479e18` | `0x44e764`, `0x44e720`, `0x44e6f0` |

The read stream (not VOL-aware) also opens the VOL archives themselves.
The stream dtors take a delete flag (2); `0x47ad78`/`0x47b09c` (DBSIM)
and `0x44edbc`/`0x44f138` (VSHELL) close through the close functions above.

### Member lookup and the path splitter

DBSIM `0x47cdf8(archive, path, out)` is the per-archive member lookup called
only by the manager search `0x468064` (`0x46807e`), which the VOL-aware
opener calls after a failed loose open (`0x473141`). It splits the requested
path with `0x47d198`, rebuilds the directory and name strings with
`0x47d2c4`, compares them with `_stricmp` (`0x47ce58`, `0x47cec6`) against
the archive's directory table, then seeks and reads the 9-byte member prefix
through the archive stream's virtual `+0x8` and `+0x18` (`0x47cf05`,
`0x47cf17`). `0x47d198` is an `fnsplit`-like splitter on CW3220 string
imports: drive (two characters when the second is `:`), directory up to the
last `\` (`_strrchr` with `0x5c`, `0x47d205`), name up to the next `.`
(`_strchr` with `0x2e`, `0x47d23e`), extension, each copied with `_strncpy`.
`0x47d2c4` is the inverse (`_strcat` of the four parts,
`0x47d2ec..0x47d31b`); the registration code also uses it to build
`root + "vol\" + "*" + name` (`0x467d38`, `0x467e49`). None of the three has
a file call of its own. Lookup rules (backslash only, case folding) are in
[vol.md](../formats/vol.md#lookup-and-loose-files); the exact contract is in
[The functions](#the-functions).

### Path helpers

| Helper | Result | Users |
|---|---|---|
| DBSIM `0x45ee00(p)` | install root `0x4d27bc` + `p` (a leading `X:` is dropped) | 35 call sites: every `data\…`, `tapes\…` and `vol\…` path |
| DBSIM `0x45ee60(p)` | CD root `0x4d280c` + `p` | help path `0x45f0c4`, voice WAV `0x436982` (see below) |
| DBSIM `0x45eec0(n)` | `tapes\` + `n` during playback (`0x4d24d6`), else `data\` + `n` | tape option handling only |
| DBSIM `0x492b90(n, ext)` | `n` up to its first `.`, then `.ext` | tape/dmp names, `data\script.dat`, DTS/DPL helpers |
| DBSIM `0x492bec(n, ext)` | `ext\n.ext` (resource convention) | 124 call sites, all resources |
| DBSIM `0x45ef1c(n, ext)` | `st<lang>\n.ext` (byte 2 of `str` replaced by the language letter) | localized STR files |
| DBSIM `0x45ef9c(n, ext)` | `simvoic<lang>\n.ext` (`0x45ef84`: the last letter of `simvoice` becomes the language letter unless that letter is `r`) | voice WAVs |
| VSHELL `0x40d99d(p)` / `0x40d9e1(p)` | install root `0x482369` + `p` / CD root `0x4823b9` + `p` | `0x40d9e1`: intro AVI `0x401cb1`, help `0x431f29` |
| VSHELL `0x408bb5(n)` | `eng\`/`fre\`/`ger\` (index `0x48235e`) + `n`, read through the VOL-aware stream | `estext.bin`, `esnames.bin`, `weapons.bin`, `missions.bin`, `wpn_desc.bin`, `wpn_info.bin` |
| VSHELL `0x40e708(i)` | `sav\` + slot name `i` from `sav\gamefile.str` | SAV load/save/scan |

**The CD-root voice path.** `0x436982` belongs to the training missions'
instructor lines: in the `CommandPort` message `0x4368xx`, used when script
header word 4 `0x4aa728` ≠ 0, the name `TM<word 4>_00<nn>` comes from the
template `0x49b7f8` (`nn` = the message counter + 1), through `0x45ef9c` →
`simvoic<lang>\TMn_00nn.wav`, prefixed with the CD root when the digit is not
`0`, and is played by `0x462940(path, 100)` if prefs byte `+0x02` (voice) is
set. The CD has 65 such files per language in `SIMVOICE`/`SIMVOICF`/
`SIMVOICG`, loose, not in a VOL.

The DBSIM extension table at `0x4a09ec` holds pointers to `nam dat bnd dts
dpl gl dmg col edg dba dci dfn gau hfn hdg hba`; all slots except `bnd`,
`dfn` and `hfn` are read by absolute address (87 reads; `dfn`/`hfn` are
passed as literals instead).

### Roots and drive.cfg

DBSIM `0x45f160` opens `data\drive.cfg` (`"rt"`, no
prefix, `0x45f19a`), reads two `%s` tokens (`0x45f1d7`, `0x45f21e`), appends
`\` to the first and stores it as the CD root `0x4d280c`; the install root
`0x4d27bc` is set to the empty string (`0x45f251..0x45f25e`) and passed to
the assert-log root setter `0x479e18`. The second token is read but not used;
its 80-byte buffer lies right below the first, so a second token of 80 or
more characters runs into the first buffer before the CD root is copied out
([launcher.md](launcher.md#game-main-0x45f160-start-up-part-0x45f1600x45f458)).
VSHELL `0x40d8df` does the same (`0x40d8f5`, `0x40d92b`, `0x40d961`; CD root
`0x4823b9`, empty root `0x482369`, log root `0x44e6f0`). Neither closes the
`drive.cfg` stream. Every `data\…` path is therefore relative to the current
directory, and the VOL registration passes the CD root as the alternate root
with the alternate-first flag 0 (DBSIM `0x45f2a8`, VSHELL `0x401b3c`):
current `vol\*.vol` first, then `<CD>\vol\*.vol`
([vol.md](../formats/vol.md#original-registration-and-lookup-order)).

## DBSIM call sites

Caller chains end at known subsystems: WinMain `0x46538c`, main routine
`0x45f160` (from `0x465462`), option parser `0x45e758`, mission setup
`0x461608` (from `0x45f42b`), logic frame `0x45f570`, frame stages
`0x401d94`, mission end `0x461ff0` (from `0x45f54b`). "Ours" names the
format or file code that covers the file: `formats/` readers, `shell/`
writers, the launcher, the VFS (`es2_vfs_*`, read-only) and the hooks of
this document; the own `es2sim` process runs all of DBSIM's start-up, load
and frame code ([own_process.md](../reference-game/own_process.md#start-up-parsers-and-file-services)).

### Startup and configuration

| Function | Sites | Caller chain | Path | R/W | Phase | Ours |
|---|---|---|---|---|---|---|
| `0x45f160` main routine | `_fopen 0x45f19a`, `_fscanf 0x45f1d7/0x45f21e` | `0x46538c` | `data\drive.cfg` (`rt`) | R | startup | setup scripts write it; `sim/src/startup.c` reads it |
| `0x45e510` video preference | `_fopen 0x45e535`, `_fread 0x45e54c` (7 bytes), `_fclose 0x45e567` | `0x45e6cc` (`-v`, early parser) and `0x46538c` (`0x4653c9`) | root + `data\prefs.cfg` (`rb`) | R | startup | hook `prefs_video`; `formats/src/cfg.c` |
| `0x45effc` language | `_fopen 0x45f013`, `_fread 0x45f02b` (1 byte), `_fclose 0x45f034` | `0x45f070` ← `0x4345cb`, `0x460821` (help) | root + `data\language.cfg` (`rt`) | R | on F1 help | launcher reads it |
| `0x4650e8` | `LoadLibraryA 0x465140` | `0x46538c` | `BWCC32.DLL` | DLL | startup | — |
| `0x473260` → `0x467cd8` | `_findfirst 0x467d4c/0x467e5d`, `_findnext 0x467e0f/0x467f20` | `0x45f160` (`0x45f2a8`) | `vol\*.vol`, `<CD>\vol\*.vol` | enum | startup | VFS (`es2_vfs_open_dbsim_order`; enumeration is the caller's job) |
| `0x47cab0` archive open | via read stream `0x47caf0` | `0x467f94` registration | each `.vol` | R | startup | `formats/vol.c` |
| `0x453878` STR reader | via VOL-aware stream `0x45389a` | `0x45f160` (`0x45f2d3`) | `error.str` | R | startup | launcher reads `error.str`; `formats/src/str.c` |
| `0x45e758` option parser | see [DBSIM recording and playback](#dbsim-recording-and-playback) | `0x45f160` (`0x45f27b`) | `.tap`, `.dmp`, restored files | R/W | startup | hooks `tape_save`, `tape_restore` (header records) |

### Mission load (`0x461608`)

| Function | Sites (all through streams unless named) | Caller chain | Path | R/W | Ours |
|---|---|---|---|---|---|
| `0x4614c0` (INIT.CPP) | read stream `0x4614fa` | `0x461608` (`0x4616b6`) | root + `data\script` + `.dat` (ext from `[0x4a09f0]`) | R | `formats/msn.c` framing, `shell/` writer |
| `0x4614c0` (INIT.CPP), second part | read stream open `0x461559` | `0x461608` (`0x4616b6`) | root + `data\player.mec` (first five words) | R | `formats/player_mec.c` |
| `0x45972c` → `0x45a1b8` → `0x459770` | read stream `0x459792`, 54 bytes | `0x45a5dc` ← `0x461608` (`0x461631`) | prefs path `0x49e81c` (root + `data\prefs.cfg`, or `tapes\prefs.cfg` in playback) | R | hook `prefs_load`; `formats/src/cfg.c` |
| `0x459d88` | `_fopen 0x459dce` (`rb`), `_fclose 0x459ddb` | `0x45a1b8` | root + `vol\simvoic<lang>.vol` (existence test; disables two prefs options when absent) | R | VFS (existence) |
| `0x45b7a8` | `GetPrivateProfileStringA 0x45b7d2/0x45b80a/0x45b842/0x45b87a` | `0x45a5dc` ← `0x461608` | root + `data\keyjoy.cfg`, section `Keyjoy`, keys `Tilt`, `Backturn`, `Missile`, `Rudder` = `Reverse` | R | hook `keyjoy_load`; `formats/src/cfg.c` |
| `0x46379c` | `GetPrivateProfileStringA 0x463806/0x463840/0x463878/0x4638ad` | `0x462410` ← `0x461608` (`0x46179f`) | root + `data\sound.cfg`, section `Sound`: `Driver` (compared with `DirectSound`), `Buffers`, `Rate`, `Width`, `Mono` | R | hook `sound_config`; `formats/src/cfg.c` |
| `0x4958fd` | `LoadLibraryA 0x49593e/0x4959b1` | `0x473708` (audio) | `sos9503.dll`, then `sos32s03.dll` | DLL | — ([audio.md](audio.md)) |
| `0x424308` world load (WORLD.CPP) | read streams `0x4243f1` (mission.var), `0x424489` (player.mec), `0x424672` (script), STR reader `0x4245f2` | `0x461608` (`0x4617c8`) | root + `data\mission.var`, `data\player.mec`, `data\mission.str`, `data\script.dat` | R | `formats/player_mec.c`, `formats/msn.c`; `shell/` produces all four ([mission.md](../formats/mission.md)) |
| `0x4253d8` | read stream `0x425466` | `0x461608` (`0x46182b`) | root + `data\script.dat` | R | as above |
| `0x42789c` terrain → `0x46c754` | VOL-aware `0x46c7b3` (`dat\mat0.dat`), `0x46c84e` (`.dba` grid), `_fopen 0x46c8e0` (`r`), `_fscanf 0x46c916` (`%d %d %d %d`) / `0x46ca13` (`%d`), `_fclose 0x46cb4f` | `0x424308` (`0x4252f7`) | `dat\zone<n>.dat`; `dat\mat0.dat`; `dba\<zone>.dba`; a non-`.dba` grid name is opened with `fopen` as a text grid | R | `formats/dat.c` (ZONE, MAT0), `formats/dbm.c`; text grid: none |
| world, objects, HUD (`0x42e010`, `0x405fac`, …) | VOL-aware stream, 49 ctor sites (`dbsim-call-args.txt`) | mission load and later | `ext\name.ext` from `0x492bec` ([Resource name families](#resource-name-families)) | R | VFS + `formats/` per type |

**Hypothesis:** the text-grid branch of `0x46c754` is a development
format. Its only literal name, `grid.dat` (`0x427b2a`), sits in the block
`0x427a97..0x427b39`, which has no static branch to it.

### Frame

| Function | Sites | Caller chain | Path | R/W | Ours |
|---|---|---|---|---|---|
| `0x45a810` input record | playback: `_fread 0x45a8c1` (8), `0x45a8eb` (16), `0x45a904` (4), `0x45a91e` (4), `0x45a9af` (14 each), `0x45a9f8` (2 each), `_fclose 0x45a976` at end of tape; record: `_fopen 0x45b2f9` (`ab`), `_fwrite 0x45b342/0x45b542/0x45b554/0x45b566/0x45b589/0x45b5a9`, `_fclose 0x45b5b2` | `0x45f570` (`0x45f583`) and modal screens `0x454c98`, `0x455fac`, `0x456cf0`, `0x457538`, `0x45866c` | `<name>.tap` (no prefix) | R/W | hook `control_record`; `formats/src/cfg.c` (entry layout) |
| `0x401e5c` stage-6 callback | `_fopen 0x401e93` (`ab`), `_fwrite 0x401ec7`, `_fclose 0x401ed0` | `0x401d94(6)` at `0x45f4a7` | `<name>.dmp` | W (append) | none |
| `0x463d4c` SFX cache | VOL-aware stream `0x463ec3`, size `0x463f14`; or `_lopen` path `0x473828` → `0x47465c` (`_lopen 0x474669`, `_llseek 0x47467c/0x474688`, `_hread 0x4746b7`, `_lclose 0x4746bd`) | sound calls `0x46254c`, `0x462b9c`, `0x463c50`, `0x464038` | sample names from the sound tables | R | [audio.md](audio.md) |
| `0x47473c`, `0x47498c`, `0x4749d0` | `_lopen 0x4747aa`, `_hread 0x47483b/0x4749ac`, `_lclose 0x474a05` | `0x473898`, `0x4739ac`, `0x474aa0` (audio streaming) | stream/music files (`HMI\`/`HMX\` + `.hmp`, `0x46234b`) | R | [audio.md](audio.md) |
| voice `0x435bf0`, `0x436244`, `0x4369a4`, `0x44aff0` | path `0x45ef9c` (`simvoic<lang>\<name>.wav`), played through `0x462940`/`0x462b9c` (SFX layer above) | radio/voice events | relative `simvoic?\*.wav` (loose, then `SIMVOIC?.VOL`); `0x436244` prefixes the CD root (`0x45ee60` at `0x436982`), i.e. the CD's loose `SIMVOICE`/`SIMVOICF`/`SIMVOICG` directories | R | VFS (loose + VOL); audio: [audio.md](audio.md) |
| `0x474360`, `0x47453c` | VOL-aware opener `0x47438b`, `0x474566` | `0x474180` | `MELODIC.BNK`, `DRUM.BNK` | R | VFS |
| alerts `0x455950`, `0x4561dc`, `0x4566e0`, `0x457538`, `0x457d38` | STR reader + `0x492bec` | modal alert screens | `st<lang>\gnl_alrt.str` …, `hba\`/`dba\` images | R | `formats/dbm.c`, `formats/src/str.c` |
| `0x459b94` prefs read-modify-write | read stream `0x459bbb`, write stream `0x459c06` (54 bytes) | `0x4574e8` (from `0x456cf0`), `0x45915c` (from `0x45866c`), `0x459df0`, `0x459b80` | prefs path `0x49e81c` | R/W | hook `prefs_store`; `formats/src/cfg.c` |
| `0x4669c4` | `WinHelpA 0x4669e4` | `0x45feb8` (`0x460827`), `0x4345a0` | `<CD>\<LANGUAGE>\es2guide.hlp` built by `0x45f070` | help | — |

### Mission end and exit

| Function | Sites | Caller chain | Path | R/W | Ours |
|---|---|---|---|---|---|
| `0x461ff0` | `_fclose 0x46200b/0x46201a` (dmp and tape streams) | `0x45f160` (`0x45f54b`) | open tape/dmp streams | close | hook `mission_end` |
| `0x42412c` results writer | write streams `0x424171`, `0x42429a` | `0x461ff0` (`0x462029`) | root + `data\results.dat`, `data\mission.var` | W (truncate) | hook `results_writer`; `shell/` reads both (`es2_shell_results`) |
| `0x459b80` → `0x459b94` | as above | `0x45f160` (`0x45f504`), when a prefs byte changed | prefs path | R/W | hook `prefs_store` |
| `0x45f160` tail | `OpenFile 0x45f524`, `0x45f53c` with `OF_DELETE` (`0x200`) | — | `tapes\prefs.cfg`, `tapes\keyjoy.cfg` (no prefix) | delete | none |
| `0x461304` cleanup | `_fclose 0x461316/0x46132e` | `0x461608` (`0x461626`) | tape and dmp streams | close | — |
| `0x496377` RTL | `_flushall 0x49638a` | RTL exit (`0x4963c7`) | all `FILE` streams | flush | — |

`0x461304` closes the dmp stream only when the tape stream was open
(`0x461313` jumps past both).

### DBSIM recording and playback

**Option parser `0x45e758`** (static) (jump table `0x45e7a3` /
`0x45e7d6`; the first name given by `-D`/`-p`/`-r`/`-d` is kept in
`0x4d24da`):

| Option | Effect |
|---|---|
| `-D` (`0x45e944`) | demo: `0x45ceb8` picks a name from `tapes\demolist.str` (read stream `0x45cf10`), sets the demo flag `0x4d2530` and playback `0x4d24d6`, opens root + `tapes\<name>.tap` `rb` (`0x45e990`) as tape `0x4d24e2`, then restores as `-p` |
| `-p<name>` (`0x45e9bd`) | playback: opens `<name>.tap` `rb` (`0x45e9e8`, no prefix, no directory), sets `0x4d24d6`; restores the tape header (below) |
| `-r<name>` (`0x45ebe4`) | record: opens `<name>.tap` `wb` (`0x45ec0e`), writes the header with `0x45cca4`, closes (`0x45ecac`), sets `0x4d24d8` |
| `-d<name>` (`0x45ecc0`) | opens `<name>.dmp` (`wb` when recording, else `rb`) as `0x4d24de` (`0x45eceb`); when recording it is closed again at once (`0x45ed0d`) and the stale pointer only serves as a flag |

#### Tape header

`0x45cca4(tape, path)` writes, for each of root +
`data\script.dat`, `data\player.mec`, `data\mission.var`, `data\prefs.cfg`,
`data\restore.dat`, `data\object.str`, `data\keyjoy.cfg` in this order: a u32
length (`_fseek`/`_ftell`/`_rewind` at `0x45cccc..0x45ccdf`; 0 when the file
is missing), then the bytes in pieces of at most 500 (`0x45cd26`,
`0x45cd3c`). Playback `0x45cd5c(tape, path)` reads the same records and
**overwrites** root + `data\script.dat`, `data\player.mec`,
`data\mission.var`, `tapes\prefs.cfg`, `data\restore.dat`,
`data\object.str`, `tapes\keyjoy.cfg` (`wb`, `0x45cd78`); the prefs path
`0x49e81c` becomes `tapes\prefs.cfg` (`0x45ea16`). Then
`0x45eafc..0x45ebd9` copies bytes 0–3 and 8–10 of `data\prefs.cfg` into
`tapes\prefs.cfg` and rewrites it. `keyjoy.cfg` is restored to `tapes\`, but
`0x45b7a8` always reads `data\keyjoy.cfg`. No DBSIM code other than these
copies names `restore.dat` or `object.str`. The layout is
[cfg.md](../formats/cfg.md#tape-files)'s; the contracts are
[tape_save and tape_restore](#contracts).

#### Per-frame record

**Byte order; field roles hypothesis.** In record mode `0x45a810`
reopens the tape with `ab` every frame and appends: once (flag `0x49ebbc`)
8 bytes from `0x477904`; a 16-byte record (words from control-record offsets
`+0`, `+0xe`, `+0x10`, `+0x12`, `+0x14`, the time-scale word `0x4d3b64`, a
dword of 17 flag bits, `0x45b35c..0x45b52d`); a u32 count N and the u32
`0x49eaa0` = M; N 14-byte entries; M words from `0x4d20c4`. Playback reads
the same sequence from the open tape; a short read or the command word
`0x412` ends playback: the tape is closed, the playback flag cleared and
`0x4d1dd6` set (`0x45a94f..0x45a993`).

#### The dmp file

`0x401e5c` (stage 6, every frame) appends `[0x49707c]` bytes of the buffer
`0x4a84fc` to `<name>.dmp` when `0x4d24de` is nonzero and recording is on,
then resets the cursor `0x497078`. The only code that adds to the buffer is
`0x401dc0`, which in playback reads the same number of bytes from the open
dmp file and compares them (`_fread 0x401dee`, `_memcmp`, message `"\n%s\n"`
into a flag `0x497080`). `0x401dc0` has no static reference, and nothing
else writes `0x49707c`/`0x497078`/`0x4a84fc`. **Hypothesis:** a determinism
check whose producers were compiled out; in this build `-d` creates an empty
`.dmp` file.

### Debug, error and unreferenced code in DBSIM

| Function | Sites | Notes |
|---|---|---|
| `0x479e8c` | `_open 0x479eb9` (`0x901`), `_write 0x479ed8/0x479ef7/0x479f06`, `_close 0x479f0f` | appends to root + `assert.log`; called by the error reporter `0x479f24` (`0x479f9d`, `0x47a000`, `0x47a09a`, `0x47a0a8`); a failed open calls `_abort` |
| `0x479cfc` | `_fputs 0x479d18` to `stderr` | allocation failure in the abort-notify registration ([mission_load.md](mission_load.md)) |
| `0x47a594`, `0x48e0b8`…`0x4913dc` region | 51 `_printf`, 54 `__fputc` to `stdout` | object dumps (`Class type: %d:%d`, `Part List:`); virtual methods of the persistence classes |
| `0x495500`…`0x49588a` | `CreateFileA 0x49551b` (`\\.\DARKMONO.VXD`), `CloseHandle 0x495550`, `DeviceIoControl` ×6 | printf to a monochrome debug monitor (`0x495614`, `_vsprintf`), from `0x45c638` and `0x47700c` |
| `0x463d4c` | `CloseHandle 0x463f2f` | error path of the SFX cache; the handle is not opened with `CreateFileA` here (role open) |
| `0x453f24` | read/write stream `0x453b51`, `0x453dc9` | writes `<counter>.SCR` and `<counter>.PAL` (screen capture, magic `:RCS`); no static reference |
| `0x4717f8`, `0x471848`, `0x4718b4`, `0x4718f8` | `mciSendCommandA` ×7 (`avivideo` open/close, open element) | no static reference; AVI playback is not reachable in DBSIM |
| `0x471628`…`0x4717c4` | `ICSendMessage` ×5 | codec calls, unreachable ([own_process.md](../reference-game/own_process.md#the-avi-code-in-dbsim-and-es2sim)); `0x47161e` is the "return 0" method of the DummyDrawable vtable `0x4a0ca0`, not AVI |
| `0x473c00`…`0x473dcc` | `mciSendCommandA` ×10 (`cdaudio`, sites `0x473c13..0x473e8b`) | CD music from `0x464858`, `0x46499c` ([audio.md](audio.md)) |
| `0x464a14`, `0x46538c` | `LoadStringA` ×4 | resource strings, not files |

`data\MECMOD#.DYN` (`0x4a6904`) has no reference.

## VSHELL call sites

Caller chains end at WinMain `0x406ab8` → startup `0x401aa4` (`0x406bd5`),
the entry-mode routine `0x40180c` (`0x401ff8`), game init `0x40e736`
(`0x401d44`), save `0x40e933`, load `0x40eaaa`, mission preparation
`0x41cd61`, results `0x40f09f`.

| Function | Sites | Caller chain | Path | R/W | Phase | Ours |
|---|---|---|---|---|---|---|
| `0x40d8df` | `_fopen 0x40d8f5` (`rt`), `_fscanf 0x40d92b/0x40d961` | `0x401aa4` (`0x401b02`) | `data\drive.cfg` | R | startup | — |
| `0x40356f` → `0x403caf` | `_findfirst 0x403d4f/0x403e52`, `_findnext 0x403e05/0x403f08` | `0x401aa4` (`0x401b3c`, mask `0x100`) | `vol\*.vol`, `<CD>\vol\*.vol` | enum | startup | VFS |
| `0x4027b2` archive open | read stream `0x4027f3` | registration `0x403fe8` | each `.vol` | R | startup | `formats/vol.c` |
| `0x428521` STR reader | VOL-aware `0x428542` | `0x401aa4` (`0x401b53`) | `error.str` | R | startup | launcher |
| `0x40796c` | `0x408bb5` | `0x401aa4` (`0x401b15`, `0x401d39`) | `<lang>\estext.bin` | R | startup | none |
| `0x40687c` (window class registration; the `BWCC.DLL` load is a side effect, never used: [vshell_process.md](vshell_process.md#2-winmain-0x406ab8)) | `LoadLibraryA 0x4068d4` | WinMain | `BWCC.DLL` | DLL | startup | — |
| `0x42f2f4` → `0x42c591` | `GetPrivateProfileStringA 0x42c5fc/0x42c61d/0x42c658/0x42c690` | `0x401aa4` (`0x401f94`) | `data\sound.cfg` (`Sound`: `Driver`, `Buffers`, `Rate`, `Width`, `Mono`) | R | startup | `formats/src/cfg.c` |
| `0x46ab1d` | `LoadLibraryA 0x46ab5e/0x46abd1`, `FreeLibrary 0x46ac64` (`0x46ac3f`) | `0x42de44` | `sos9503.dll`, `sos32s03.dll` | DLL | startup | — |
| `0x40e380` | read stream `0x40e3a0` | `0x40e736` | `sav\gamefile.str` | R | startup | `formats/src/str.c` |
| `0x411e40`, `0x413c4c` | VOL-aware `0x411e5f`, `0x413c71` | `0x40e736` | `gam\herc_inf.dat`, `gam\damage.dat` | R | startup | `formats/dat.c`, `shell/` |
| `0x4309f0` (stats) | read stream `0x430a35` | `0x40e736` | `sav\<slot>` for each save slot (`0x40e708`) | R | startup | `formats/sav.c` |
| `0x40dc5b` / `0x40dd0a` / `0x40dfa3` | read stream `0x40dc84`; write stream `0x40dd30`; both `0x40dfd1`/`0x40e03a` | prefs init `0x40dc44` ← `0x406b28`; writes from option screens, movie player `0x41eda3`, sound init `0x42f4a7`, help `0x431e9d` and others | `data\prefs.cfg` | R/W | startup, shell | `formats/src/cfg.c` |
| `0x41e98c` movie player → `0x41e51b`, `0x41e57c`, `0x41e5bb`, `0x41e640` | `mciSendStringA` ×7 (`open avivideo`, `open %s alias mov style child parent %d`, `status mov window handle`, `play mov window notify`, `close …`), `_scanf 0x41e6cd` | `0x40180c` (`0x40198f`), `0x401aa4` (`0x4020b2`), results `0x40f09f` (`0x40f5c7`), `0x431c7b` | `<CD>\avi\pt1.avi` (intro, `0x401cac`); 86 names `avi\pt1.avi`…`avi\dropship.avi` stored back to back at `0x471136..0x471568`, indexed through the pointer table `0x470fb4` ([vshell_ui.md](vshell_ui.md)) | R (MCI) | startup, shell | own IV32 binding (`media/`) |
| `0x40e503` | read/write stream `0x40e525` | save `0x40e6cd` ← `0x40e933` | `sav\gamefile.str` | W (no truncation) | shell | `formats/src/str.c`, `formats/sav.c` |
| `0x40e933` save | read/write stream `0x40e996` | save callers (`0x40219b`, `0x40f561`, menus) | `sav\<slot>` | W (no truncation) | shell | `formats/sav.c` (`es2_sav_write`), `shell/` |
| `0x40eaaa` load | read stream `0x40eb05` | `0x40180c` (`0x4018af`, slot 10), menus | `sav\<slot>` | R | shell | `formats/sav.c`, `shell/` |
| `0x413095` / `0x4131e3` → `0x40da8d` copy | read stream `0x40daac`, write stream `0x40daec` | save `0x40e9e1` / load `0x40eb5a` | `data\script.dat` ↔ `sav\script%d.dat`, `data\mission.str` ↔ `sav\missn%d.str`, `data\player.mec` ↔ `sav\player%d.mec` | R/W (truncate) | shell | `shell/` (save/restore model) |
| `0x41818b`, `0x417cb0` | VOL-aware `0x4181ab`, `0x417d3b` | `0x41cd61` | `msn\<stem>.msn`, `.eng/.fre/.ger` | R | preparation | `formats/msn.c`, `shell/` |
| `0x418014` MISSION.STR writer | read/write stream `0x418047` | `0x41cd61` and the debrief path | `data\mission.str` | W (no truncation) | preparation, results | `shell/` |
| `0x41b278` script writer | read/write stream `0x41b2a8` | `0x41cd61` (`0x41ce20`) | `data\script.dat` | W (no truncation) | preparation | `shell/` |
| `0x40f68c` → `0x40ef83` | `_remove 0x40f6b0`; write streams `0x40f6cc`, `0x40efa2` | `0x41cd61` (`0x41d058`), `0x44db5c`, `0x44dc4d`, `0x431935`, `0x445b99` | `data\player.mec` (deleted, then written), `data\mission.var` | W (truncate) | preparation | `shell/` |
| `0x40f011`, `0x40f09f` | read streams `0x40f030`, `0x40f0c6` | `0x40180c` (`0x4018b7`) | `data\mission.var`, `data\results.dat` | R | results | `shell/` (`es2_shell_results`) |
| `0x424a2b`, shell map `0x4242a0`…`0x425404` | read stream `0x424a73`; VOL-aware `0x4242c0`, `0x424374`, `0x425244`, `0x425442`, `0x425516` | map screen | `data\script.dat`, `data\mission.str`, `data\maplabel.str`, `dat\<zone>.dat`, `dba\<zone>.dba`, `data\player.mec`, `data\mforms.dat`, `dba\mis_icon.dba`, `dba\mapshp.dba`, `dfn\map.dfn` | R | shell | `formats/dat.c`, `formats/dbm.c` |
| `0x429664` HeightGrid | VOL-aware `0x4296b4`, `0x429798`; `_fopen 0x429894`, `_fscanf 0x4298fd/0x429a6b`, `_fclose 0x429b81` | `0x42519c` (map) | `data\mat0.dat`, `.dba` grid, else text grid (`%d %d %d %d`) | R | shell | `formats/dat.c` (MAT0) |
| `0x42cbbf` SFX cache, loader `0x42eaf7` | VOL-aware stream `0x403065` (cache) / `0x4032b1` (loader); `CloseHandle 0x42ce4b` | shell sound (`0x42ca37`, `0x42cf77`) | `hmi\*.wav` (`0x42f3bd…`; the shell's effects and music, from `SHLSOUND.VOL`) | R | shell | [audio.md](audio.md#files-and-playback-format) |
| `0x42ede6`, `0x42eec0`, `0x42f14c`, `0x42f18d` | `_lopen 0x42edf2/0x42ef42`, `_llseek` ×2, `_hread` ×3, `_lclose 0x42ee45/0x42f1ba` | the unused MIDI-mode path (slot flag bit 0; the MIDI half is disabled at `0x42de9c`/`0x42deb6`) | `MELODIC.BNK`, `DRUM.BNK` (`0x42e933`/`0x42e94a`), never loaded | R (dead) | shell | none ([audio.md](audio.md#shell-audio-in-short)) |
| `0x42e2f9`…`0x42e4d6` | `mciSendCommandA` ×10 | only the wrappers `0x42dc25`, `0x42dc40`, `0x42dc53`, `0x42ddf4`, `0x42de07`, which have no caller (dead code; `0x42d9c3`/`0x42dcf9` are `SFX::setLooping`/`stopAll`, [audio.md](audio.md#shell-audio-in-short)) | `cdaudio` device | CD (dead) | shell | — |
| `0x408d69` | read stream `0x408d88` | help `0x431e79` | `data\language.cfg` | R | shell | launcher |
| `0x407953` | `WinHelpA 0x407963` | `0x431e79` (`0x431f30`) | `<CD>\english\`/`french\`/`german\es2guide.hlp` | help | shell | — |
| `0x44e764` | `_open 0x44e793` (`0x901`), `_write` ×3, `_close 0x44e7e8` | assert reporter | `assert.log` | W (append) | error | — |
| `0x44e5d4` | `_fputs 0x44e5f0` | assert notifier | `stderr` | — | error | — |
| `0x46b5df` | `_flushall 0x46b5f2` | RTL exit | — | flush | exit | — |

Other VOL-aware resource readers of VSHELL (`vshell-call-args.txt`):
`gam\hercs.dat` (`0x410b11`), `gam\weapons.dat` + `<lang>\weapons.bin`
(`0x4125e8`), `gam\career.dat` + `<lang>\missions.bin` (`0x412f2a`),
`<lang>\campaign.str` (`0x40fd2d`), `gam\arm_hots.dat`/`rpr_hots.dat`
(`0x43c830`), `dba\arm_weap.dba` + `gam\arm_weap.dat` (`0x43ebbc`,
`0x4484c4`), `<lang>\esnames.bin` (`0x410055`), plus the UI image and
palette tables (`dba\…`, `dbm\bay2a_8*.dbm`, `dpl\…`, `gam\rpr_*.dat`,
`gam\arm_*.dat`) listed in `vshell-path-strings.txt`.

Debug and dead code: `0x4015d8` prints the option usage with `_printf`
(11 sites); `0x4046ac`, `0x4600c0`…`0x463a4c` dump objects to `stdout`
(54 `__fputc`, 51 `_printf`; `0x41df0e` prints one more line);
`0x468fe4`…`0x469a18` print window events; `0x46a720`…`0x46aaaa` drive
`\\.\DARKMONO.VXD` (`CreateFileA 0x46a73b`, `CloseHandle 0x46a770`, six
`DeviceIoControl`), used by the debug printf `0x46a834`. The speech-volume
test `0x40dbdf` (`_fopen` `vol\espeech.vol`, `fspeech.vol` or `gspeech.vol`
by language, `_fclose 0x40dc32`) has no static reference; those three
archives are not on the CD. The `_scanf` call `0x41e6cd` passes `(buffer,
"%d", &value)`, the shape of `sscanf` (**hypothesis:** the import is used as
`sscanf`; with no console, no file is read).

## Resource name families

DBSIM never names a resource with a literal path:
every resource name comes from `0x492bec(name, ext)` = `ext\name.ext`
(124 sites), `0x45ef1c`/`0x45ef9c` (localized STR and voice WAV), or the
`data\…` helpers. The extensions passed are `dat`, `dts`, `dgs`, `dpl`,
`gl`, `dmg`, `col`, `edg`, `dba`, `hba`, `dci`, `dfn`, `hfn`, `gau`, `hdg`,
`pdg`, `ofs`, `snc`, `wld`, `cyc`, `rmp`, `str`, `wav`, `nam`. Archive
members in VOL are stored under exactly these directory names
([vol.md](../formats/vol.md)), so the VFS must keep `dir\name.ext` with a
backslash. VSHELL mostly uses literal `dir\name.ext` strings and
`<lang>\name.bin`.

Loose-only access (never VOL): DBSIM `fopen` of `data\drive.cfg`,
`data\prefs.cfg` (`0x45e510`), `data\language.cfg`, `vol\simvoic?.vol`,
tapes, dmp, text grid; `GetPrivateProfileStringA` of `sound.cfg` and
`keyjoy.cfg`; all read/write/truncating streams (launch files, prefs,
results, SAV). Only the VOL-aware stream consults archives
([vol.md](../formats/vol.md#lookup-and-loose-files)).

## BND files

**Extending [bnd.md](../formats/bnd.md) (static):** none of the
134 DBSIM and 73 VSHELL file call sites, none of the 124 `0x492bec` path
constructions and no `_sprintf` format names a `.bnd` file; the BND slot
`0x4a09f4` of the DBSIM extension table is never read, while the adjacent
slots are read 87 times. A case-insensitive search finds the bytes `bnd`
once in DBSIM (the table literal `0x4a0a3a`) and never in VSHELL. In the
CD data, all 85 occurrences of `.bnd` in `SIMVOL0.VOL` and the one in
`SIMPATCH.VOL` lie in the archives' name tables (`SIMVOL0` offsets
`0xd8..0x69f`); no member payload, `DATA` or `TAPES` file names a BND file.
**Hypothesis (strengthened):** the retail executables do not load BND files;
the matching compiled tables in bnd.md replace them. A constructed name that
this inventory missed (computed strings are not excluded) would be the only
way a BND reader runs.

## Own code per file area

| Area | Covered by | Gap |
|---|---|---|
| VOL enumeration, priority, member lookup | `formats/vol.c`, `es2_vfs_open_dbsim_order`, the `vfs` hooks | enumeration order and roots are the caller's job |
| Resource readers (DAT, DBM/DBA/HBA, DPL, DTS, MSN, collision, STR) | `formats/` | none: the packed string files (`error.str`, `mission.str`, `gamefile.str`, alerts, `campaign.str`) are read and written by `formats/src/str.c` ([str.md](../formats/str.md)) |
| Launch files and results | `shell/` (byte-exact preparation and results path; `shell/src/engine_io.c` writes `script.dat` and `mission.str` from offset 0 without truncation, as VSHELL does) | a byte-exact comparison against a reused `DATA` directory must account for stale tails |
| SAV and copies | `formats/sav.c` (`es2_sav_write_over` models the stale tail), `shell/` | none |
| Configuration (`drive.cfg`, `prefs.cfg`, `keyjoy.cfg`, `sound.cfg`, `language.cfg`) | launcher reads `language.cfg` and `error.str`; setup scripts write `drive.cfg`/`language.cfg`; `formats/src/cfg.c` reads and writes `prefs.cfg` (54 bytes) and reads `keyjoy.cfg`/`sound.cfg` ([cfg.md](../formats/cfg.md)); the `config` hooks | the `GetPrivateProfileStringA` name resolution stays open ([Open questions](#open-questions)) |
| Tapes, dmp, `assert.log`, screen capture | tape layout in `formats/src/cfg.c`; hooks `tape_save`, `tape_restore`, `control_record` | dmp, screen capture: none |
| Audio files (`_lopen`, SFX cache, BNK, HMP) | — | [audio.md](audio.md) |
| Movies (AVI via MCI) | — | own IV32 binding (`media/`) |

A VFS that replaces these call sites needs: a read view (loose first, then
archives) for the VOL-aware stream; plain loose read, truncating write,
non-truncating read/write and append for the other streams; delete
(`_remove`, `OpenFile(OF_DELETE)`); `findfirst`/`findnext` over `vol\`;
an existence test; and a CD-root view for help, movies, one voice path
and the second VOL pass. All paths use `\` and are relative to the
process's current directory.

## Open questions

- **Open:** `GetPrivateProfileStringA` gets `data\sound.cfg` and
  `data\keyjoy.cfg` without a drive or leading backslash (the install root
  is empty). The Win32 documentation searches the Windows directory for
  names "without a full path"; whether Windows 95/NT resolves these relative
  names against the current directory or the Windows directory decides
  whether the installed `DATA\SOUND.CFG` and `DATA\KEYJOY.CFG` are read at
  all. With the file absent, DBSIM's `Driver` default is empty (not
  `DirectSound`), i.e. MME ([cfg.md](../formats/cfg.md#ini-files-soundcfg-and-keyjoycfg)).
  Live check: an API trace of the two calls or a changed `Buffers` value on
  a clone.
- **Hypothesis:** the 7-byte `prefs.cfg` read at `0x45e510` and the
  54-byte prefs block share one file layout ([cfg.md](../formats/cfg.md#prefscfg)).
- **Hypothesis:** `restore.dat`/`object.str` are leftovers of an older
  save mechanism; only the tape code names them.
- **Open:** `EXIT.CFG` (CD `DATA`) is not named by ES, VSHELL or DBSIM
  (case-insensitive byte search); its writer is unknown.
- **Open:** the role of the `CloseHandle` in DBSIM's SFX cache error path
  (`0x463f2f`, handle `[ebp-0x54]`).
- **Open:** whether the `AVF`/`AVG` CD directories (French/German movies)
  replace `avi\`; not traced.

## Evidence

The sites were collected from the `llvm-objdump` listings of DBSIM, VSHELL and
CW3220.DLL:

- every site per file-related import;
- the sites grouped by owner;
- the caller chains (depth 4) of every function in
  [DBSIM call sites](#dbsim-call-sites) and [VSHELL call sites](#vshell-call-sites);
- the string arguments at each stream/path-helper call;
- the path-like strings and their references.

## The VOL-aware read stream as own code

Eight functions of DBSIM's VOL-aware read stream are hooks of the adapter
group `vfs`, served from own code: `stream_open` `0x4730e8`, `stream_close`
`0x47307c`, `stream_read` `0x4735b8`, `stream_seek` `0x4733a4`,
`stream_tell` `0x473368`, `stream_size` `0x473334`, `vol_lookup` `0x468064`
and `member_lookup` `0x47cdf8`. The contract was read from the
`llvm-objdump` listing. The constructor+open `0x472f14`, the default
constructor `0x472e6c` and the registration (`0x473260`, `0x467cd8`,
`0x467f94`, `0x47cab0`) stay original; they reach the hooked functions
through ordinary calls. The destructor `0x472f64` is the hook
`stream_destroy` of group `assets`, together with the persistent-object
loader `0x47a144` and the bitmap classes, which read member streams through
`es2_vfs_stream_fetch` (the member path of the read below, without its
copy): [asset_loading.md](asset_loading.md).

**Where the code lives.** `formats/` holds what depends only on the VOL
bytes: `es2_vfs_dbsim_member` (`formats/src/vfs.c`) applies the original's
per-archive lookup rules to an `es2_vol`. `core/src/vfs_stream.c` (module
`vfs_stream`, depends on `//formats`) holds the game-object contract: the
stream, buffered-stream, archive and manager fields over guest memory, the
result registers and the runtime calls. The adapter
`tools/reference-game/proxy/vfs_hooks.h` identifies archives, maps their
files and makes the guest calls. `formats/` stays free of game objects and
I/O.

### Objects

Stream S, `0x2f` bytes, vtable `0x4a0e10` (slots: `+0` destructor
`0x472f64`, `+4` tell, `+8` seek, `+0xc` `0x473510` mode query, `+0x10`
`0x473578` (AL = 1), `+0x14` `0x4735a4` write (status 4, EAX 0), `+0x18`
read):

| Offset | Width | Meaning |
|---|---|---|
| `+0x04` | 4 | status: 0 ok, 1 open/read error, 2 end, 3 close error, 5 closed |
| `+0x08` | 2 | runtime handle (`_open`), `0xffff` = none (member stream) |
| `+0x0a` | 4 | the archive's buffered stream B (`[A+4]`) of a member, else 0 |
| `+0x0e` | 4 | source: B for kind 2, the decoder for kinds 7/9, else 0 |
| `+0x12` | 4 | member payload start in the archive file (block + 9) |
| `+0x16` | 4 | position inside the member |
| `+0x1a` | 4 | the archive position at which the stream last left B |
| `+0x1e` | 4 | member kind (the block's first byte) |
| `+0x22` | 4 | member length (block bytes 1..4) |
| `+0x26` | 1 | "opened" flag (open sets 1; close closes the handle only when it is set) |
| `+0x27`, `+0x2b` | 4 | decoder objects for kinds 7 (`0x92` bytes, ctor `0x47b50c`) and 9 (`0x28` bytes, ctor `0x47b840`), kept across opens |

Buffered stream B (`0x21` bytes, vtable `0x4a1ad8`, built by `0x47c644`
over the archive's read stream `[A+0]` with a `0x400`-byte buffer): `+4`
status, `+8` the file stream, `+0xc` buffer size, `+0x10` file position of
the buffer, `+0x14` buffer, `+0x18` owns the buffer, `+0x19` valid bytes,
`+0x1d` cursor. Tell `0x47c7bc` = `+0x10` + `+0x1d` (status 6: 0). Seek
`0x47c7d8` resets status 2 to 0, fails (EAX 0) for any other nonzero
status, moves the cursor inside `[base, base+valid]` (signed compares) or
refills (`0x47c8c4`: file seek, read of the buffer size; a short read at
the file end keeps the bytes it got, none gives status 2 and AL 0). Read
`0x47c82c` (B, n, buf) copies from the window and refills. Archive A
(`0x30` bytes, `0x47cab0`): `+0` read stream, `+4` B, `+8` the path
(`strcpy`; see vol.md for the overlap of paths longer than 19 characters),
`+0x1c` priority, `+0x1d` directory count, `+0x1e` member count (word),
`+0x20` use mask, `+0x24` directory string pointers, `+0x28` the directory
bytes, `+0x2c` the 18-byte member records as in the file. Manager M
(`[0x4a0d18]`): 30 slots, count byte `+0x78`.

### The functions

All are cdecl. "AL 1" means the original sets AL and leaves bits 8-31 of
EAX from the preceding instruction; those bits are given where they follow
from the state.

- **close `0x47307c` (S).** If `+0x26` is set and the handle is not
  `0xffff`: `_close` (sign-extended handle); -1 gives status 3 and EAX 0,
  nothing else. Then the reset of each decoder (`0x47b5cc(+0x27)`,
  `0x47b9cc(+0x2b)`, objects kept), `+0x0a`, `+0x0e`, `+0x16` <- 0, handle
  <- `0xffff`, status <- 5, EAX = `S+0xa` with AL 1 (`lea eax,[ebx+0Ah]`).
  `+0x12`, `+0x1a`, `+0x1e`, `+0x22`, `+0x26` are kept.
- **open `0x4730e8` (S, path).** Exception frame `0x49630c` (record at
  `[ebp-28h]`; EDX at exit = the record's saved `FS:0`, i.e. `FS:0` at
  entry, on both exits). close(S); `+0x0a/+0x0e/+0x16` <- 0; handle <-
  `_open(path, 0x8000)` (word). A handle other than -1 succeeds as a loose
  stream (EAX = the `_open` result with AL 1). Otherwise, with M =
  `[0x4a0d18]` non-null, `vol_lookup(M, path, S+0xa)`; AL 0 or no manager:
  `+0x0a/+0x0e/+0x16` <- 0, status 1, EAX 0. Found: kind 2: `+0x0e` <-
  `+0x0a`, EAX 1; kind 7: if `+0x27` is 0, `+0x27` <- `new(0x92)` with the
  ctor and the construction counter `[[0x6cbee4]]` -2 (a null allocation
  stores 0), then `+0x0e` <- `+0x27`, EAX = `0x47b594(+0x27, +0x0a)`; kind
  9 likewise with `0x28`, `0x47b840`, `0x47b974`, `+0x2b`; any other kind:
  `+0x0e` stays 0, EAX = kind-9. Every success: status 0, `+0x26` <- 1,
  AL 1.
- **vol_lookup `0x468064` (M, path, R).** For i = 0 while (byte) i <
  `[M+0x78]` (reread each round): `member_lookup([M+4i], path, R)`; the
  first AL other than 0 returns its EAX (AL 1); none: EAX 0.
- **member_lookup `0x47cdf8` (A, path, R).** Splits the path with
  `0x47d198` into static buffers (`0x6c5aa8` drive, `0x6c5aab` directory,
  `0x6c5bab` name, `0x6c5cab` extension) and rebuilds drive+directory and
  name+extension with `0x47d2c4` into `0x6c5dab`. The directory part is
  compared (`_stricmp`) with the directory strings in table order and the
  first equal one wins (an empty directory part uses index `0xff` without a
  search; none equal: EAX 0); then the records with that directory byte are
  compared by name in table order (`_stricmp` from the slot start). Found
  (record i, block offset b = the dword at record `+14`): `R+8` and
  `R+0x10` <- b+9 (`+0Dh-4`), B.seek(b), B.read(9) into a stack buffer,
  `R+0x14` <- the kind byte, `R+0x18` <- the length dword, `R+0` <- B;
  EAX = the length with AL 1. `R+4` and `R+0xc` are never written. Not
  found: EAX 0, B untouched. The rules are `es2_vfs_dbsim_member`'s
  (vfs.h): ASCII letters fold, only the backslash separates. The static
  buffers are scratch: only `0x47cdf8` and the registration `0x467cd8`
  reference them (immediates `0x6c5aa8..0x6c5dab`), and each writes them
  before reading; the replacement does not write them.
- **tell `0x473368` (S).** Handle other than -1: `_lseek(h, 0, 1)`. Else
  `+0x16` if `+0x0e` is not 0, else 0.
- **size `0x473334` (S).** Handle other than -1: `_filelength(h)`. Else
  `+0x22` if `+0x0e` is not 0, else 0.
- **seek `0x4733a4` (S, pos).** Status 2 <- 0 (EAX 0); any other nonzero
  status: EAX 0. AX <- handle. Handle other than -1: `_lseek(h, pos, 0)`;
  -1 gives status 1 (`0x473580` always stores 1: its test reads the
  `_errno` thunk bytes,
  [rtl_imports.md](../reference-game/rtl_imports.md#45-two-link-artefacts-_errno-and-__streams))
  and EAX 0, else EAX = result+1 with AL 1. `+0x0e` = 0: AL 1 (EAX high
  word from the entry or 0 after the reset, AH `0xff`). Otherwise `+0x16`
  <- pos; kind 2: `+0x1a` <- `+0x12`+pos if pos < `+0x22` (unsigned), else
  `+0x1a` <- `+0x22` (the length itself, not start+length) and status 2;
  B.seek(`+0x1a`), EAX = B's seek result. Kinds 7/9: decoder reset,
  `+0x1a` as for kind 2, B.seek(`+0x12`), decoder init, EAX = the decoder's
  seek(pos). Other kinds: AL 1.
- **read `0x4735b8` (S, size, buf).** Status other than 0: EAX 0. Size 0:
  AL 1 over the entry EAX. Handle other than -1: `_read(h, buf, size)`;
  -1: status 1, EAX 0; short: status 2, EAX 0; else EAX = size+1 with AL
  1. Member: if B.tell differs from `+0x1a`, the stream's own seek (virtual
  `+8`) to `+0x16`. Then, with pos = `+0x16` (reread): size+pos <= `+0x22`
  (32-bit sum, unsigned): source.read(size, buf), `+0x16` += size, `+0x1a`
  <- B.tell, EAX = that tell with AL 1. Else status 2,
  B.read(`+0x22`-pos+1, buf) (from B even for decoder kinds; the extra byte
  is the member's suffix byte when B was in place), `+0x16` <- `+0x22`+1,
  `+0x1a` <- B.tell, EAX 0. The resync seek's past-end branch leaves B at
  the archive offset `+0x22`, so a read at the member end after another
  stream moved B reads the archive byte at offset "length", not the suffix
  byte; the replacement does the same.

The stream read never tests the result of B's read; B's results enter only
through its tell.

### What the replacement does

- **Archives.** The adapter identifies an archive object A by its path
  (`A+8`, at most 19 characters with the terminator inside the first 20
  bytes): it opens the same relative path (in the game: from the working
  directory, as the game's `_open` did), maps the file read-only, parses
  it with `es2_vol_open`, and accepts it only when A's in-memory tables
  equal the file's (`es2_vfs_stream_archive_matches`: counts, priority,
  every record, every directory string). Member bytes, kinds and lengths
  come from that mapping. A buffered stream is identified through the
  archive that holds it (cache, then the manager slots).
- **B after an own operation.** The original's B ends at a known logical
  position (after the lookup: b+9; after a read: P+count; after a seek: the
  new `+0x1a`). The replacement leaves B as an empty window at that
  position (status 0, base = position, valid 0, cursor 0) and neither reads
  nor refills it. B's tell is then the original's, and B's next seek or
  read by original code refills from the file at the right place (a seek to
  the base keeps the cursor at 0 without a refill; a read finds the window
  empty and refills at the base). Its buffer bytes and window bounds differ
  from the original's; nothing outside B's own functions reads them. The
  archive file stream's position, and its status 2 after a short refill,
  differ too; B's next refill seeks first and resets both.
- **Reads.** Kind-2 member bytes are copied from the mapping at the
  archive position P (B's tell, or the resync target), count = size or the
  past-end count. The resync seek is done inline (counted as
  `hk_stream_read_call_4733a4`), so a replaced read does not enter the
  seek hook.
- **Calls kept.** In replaced mode open calls close `0x47307c`, the
  runtime `_open` and `vol_lookup` `0x468064` through the guest bridge, and
  `vol_lookup` calls `member_lookup` `0x47cdf8`: nested hooks keep their
  modes and their counters. Loose streams keep `_read`, `_lseek`,
  `_filelength` and `_close` (the runtime file model of
  [rtl_imports.md](../reference-game/rtl_imports.md) runs below them);
  kinds 7/9 keep the decoder calls of open and close.
- **Not modeled (pass-through).** A call whose state the own code does not
  cover runs the original through the trampoline and counts `bypassed`:
  member_lookup on an unidentified archive or with a path of 256 or more
  bytes (the original's static buffers would overflow), B in an error
  state (anything but 0 or 2; for a read without a resync anything but 0),
  a read or seek whose archive range leaves the file, read and seek of
  decoder kinds. The decision is made before the first write. In the suites
  none occurs (all archives come from the working directory's `vol\`); an
  archive registered from a path of 20 or more characters would not be
  identified.
- **Results.** EAX as above, including bits 8-31 for open, close, read,
  tell, size and both lookups; seek: AL (bits 8-31 are B's seek result and
  depend on its window; the replacement returns B with AL 1, the in-window
  value). EDX: open = `FS:0` at entry; the others 0. ECX is not reproduced.
  A scan of every direct call site of the eight functions and of `0x472f14`
  finds no read of
  ECX or EDX before a write (the `sete cl`/`sete dl` and `mov cx,[..]` after
  the calls are partial writes followed by masking or shifts by CL of the
  new value); the virtual call sites (`[vtable+4/8/18h]`) were not
  enumerated.
- **Not reproduced (stack).** open's frame record (`[ebp-28h]..`) and
  member_lookup's 9-byte buffer stay below the caller's ESP afterwards; the
  replacement leaves other residue there. No caller was found to read it,
  and the suites stay identical with the hooks replaced.

### Compare mode

All eight accept compare. The replacement phase is pure (`commit` 0): it
writes only the clone of the declared span (S, `0x2f` bytes; for the two
lookups the record R, `0x1c` bytes), makes no runtime or guest call, and
the read compares its bytes with the original's buffer (the real `buf`,
which holds the original's output at that point) instead of writing them
(`vfs_last_differences`: a byte-for-byte check of every read, stronger
than a hash). Read, seek and member_lookup declare B as a region
(`ARG_INDIRECT`: S+0Ah resp. A+4, `0x21` bytes) so that the replacement
sees B's pre-state; the comparators ignore the region bytes (the
representations differ by design). Comparators: open EAX, EDX and S; close
and seek AL and S (close's bits 8-31 are the stream's address, which
differs for the clone); the others EAX and the span; a read with byte
differences is a mismatch. A call that would need a runtime or decoder
call in the replacement phase (an existing loose file, probed with
`CreateFileA` and `_open`'s access and share flags; an owned handle to
close; `_read`/`_lseek`/`_filelength` on a loose stream; decoder kinds; an
unidentified archive) is SKIP: the comparator accepts and the adapter
counts `skipped`. The framework itself skips read, seek and member_lookup
when S+0Ah (A+4) is 0, i.e. for loose and closed streams.

With all eight in compare, open's original phase bypasses the nested close
and lookups (close still compares when the destructors call it, the lookups
never), so `vol_lookup` and `member_lookup` compare only in single-hook runs
(`-Hooks "vol_lookup=compare"`, `-Hooks "member_lookup=compare"`).

### In vitro

`invitro/vfs_cases.h` (full tier: 200,000 direct cases per hook; quick
tier: 100,000). The harness maps DBSIM; DBSIM's `_open`, `_read`, `_lseek`,
`_filelength` and `_close` go to an own file system over the real CD tree
under `ES2_DATA_ROOT` (whole files read once; handles, positions and fault
injection for non-archive handles in a saved state), `_calloc` to a bump
heap, and the lookup's string imports to the privately mapped CW3220.DLL.
The manager is built by the original insert `0x467f94` over the nine
simulation archives of the CD's `VOL` directory (2,963 member paths); a
second manager holds a synthetic archive (kinds 2, 4, 7, 9; served only by
the test file system, so the adapter cannot identify it) and SIMPATCH.
Paths: members in exact, folded and broken spellings ('/', drive, missing
directory, truncated, extended), loose CD files (`DATA\PREFS.CFG`,
`vol\SIMPATCH.VOL`, ...), missing names, 250-269-byte paths and the
synthetic kinds. States: fresh, member and loose streams moved by original
reads and seeks, other streams and lookups moving B, status and field
variations, B in states 2/3/6 (3/6 only where no lookup follows: the
original's lookup then reads stack garbage), decoder objects and fault
injection. Compared per case: EAX (seek: AL), EDX of open, the arena with
S, S1, R and the path, the read buffer with guards, every B's tell, status
and fixed fields, every archive file stream's vtable, handle and flag, the
test file system's handles (positions of non-archive handles), the heap,
the construction counter, `[0x6bb30c]` and the decoder stub log. Not
compared: B's window, the archive file streams' status and positions, the
split buffers.

Further passes per hook: 256 installed 31/a7 poison cases (fast and
preserving dispatch); compare mode alone (exact cases: original result and
state kept, 0 mismatches; skewed cases: EAX bit 0, for read a flipped byte
of the original's buffer, one mismatch per compared call); for stream_open
and stream_read real-callee cases with the eight hooks in all 256
original/replaced combinations, each followed (when no B is in an error
state) by an original open and read from both end states, which shows that
B's empty window serves the original. All 47 branch sites of the eight
functions are covered both ways (`branch_sites.h`); none is proved. One
caught mutation per hook in `check_mutations.ps1` (MUTATIONS.md).

Synthetic tests: `//core:vfs_stream_test`, `//formats:vfs_dbsim_test` (the
lookup rules) and `//formats:vfs_dbsim_real_test` (all 3,163 members of the
13 CD archives are found under their own directory and name, folded too,
never with '/').

### Live results

With all hooks replaced every suite is identical to its golden and the
eight `vfs` hooks are never skipped or bypassed; `stream_seek` is never
called directly (vtable slot only). The compare run with the eight hooks in
`compare` has 0 mismatches, skipped only where a call needs runtime or
decoder calls; the lookups compared alone have 0 mismatches and 0 skipped.

### Open points

- The decoder paths (kinds 7/9) are covered in vitro with stubbed decoders
  only. No shipped member uses them: all 3,802 members of the 16 CD archives
  are kind 2 (`tools/reference-game/scripts/domain_member_kinds.py`).
- After an own operation the buffered stream's window differs from the
  original's (empty window); a future own reader of B's buffer would have
  to model it.
- An I/O error inside an archive file (B in status 1/3/6) is not modeled;
  such calls pass through.

## Configuration and tape files as own code

Seven functions of the DBSIM call-site tables are hooks of the adapter
group `config`, served by own code in `core/src/config.c` (module `config`,
header `es2/core/config.h`; the prefs and tape layout constants come from
`formats/cfg`, [cfg.md](../formats/cfg.md)):

| Hook | VA | Arguments | Caller (site) | Phase | Live after early arming |
|---|---|---|---|---|---|
| `prefs_video` | `0x45e510` | mode byte | WinMain `0x4653c9` (0), option `-v` `0x45e72e` | start-up | no (before `0x45f42b`) |
| `prefs_load` | `0x459770` | flag | `0x45a1b8` (`0x45a20d`) ← `0x45972c` ← input set-up `0x45a5dc` ← mission load `0x461631` | mission load | yes, once |
| `prefs_store` | `0x459b94` | count, index list | joystick defaults `0x459df0` (`0x45a197`), options screen exit `0x4574e8` (`0x4574f2`), control mapping `0x45915c` (`0x459238`), exit `0x459b80` (`0x45f504`) | load, frame, exit | yes, see [Live results](#live-results-1) |
| `keyjoy_load` | `0x45b7a8` | — | `0x45a5dc` (`0x45a7f9`) | mission load | yes, once |
| `sound_config` | `0x46379c` | object | `0x462410` (`0x4624ed`) ← mission load `0x46179f` | mission load | yes, once |
| `tape_save` | `0x45cca4` | tape FILE*, path | option parser `-r` (`0x45ec25` … `0x45eca3`, seven calls) | start-up | no |
| `tape_restore` | `0x45cd5c` | tape FILE*, path or NULL | option parser `-p`/`-D` (`0x45ea35` … `0x45eae3`, seven calls) | start-up | no |

The contract was read from the `llvm-objdump` listing of the reference
DBSIM. `0x459838` (a whole-block writer through the write stream) has no
static reference and is not hooked. The per-frame tape entries are written
and read inside `control_record` `0x45a810`
([input.md](input.md)); this group covers the header records, and
`formats/cfg` checks the entry layout against the CD tapes
([Tape layout in formats/cfg](#tape-layout-in-formatscfg)).

### Contracts

Calls named "thunk" go through DBSIM's import thunks: `_fopen` `0x496802`,
`_fread` `0x49684a`, `_fwrite` `0x496832`, `_fclose` `0x496838`, `_fseek`
`0x49682c`, `_ftell` `0x4967de`, `_rewind` `0x496772`, `_memset`
`0x49681a`, `_stricmp` `0x496868`, `_atol` `0x496880` (CW3220) and
`GetPrivateProfileStringA` `0x496910` (KERNEL32, stdcall, six arguments).
The read stream (`0x47ad40` ctor+open with `_open(path, 0x8000)`, `0x47af98`
read, `0x47ad78` destructor) and the write stream (`0x47b064` ctor+open,
truncating; `0x47b2b8` write; `0x47b09c` destructor) are the classes of
[The stream library and path helpers](#the-stream-library-and-path-helpers);
their object is 0Ch bytes in the caller's frame (`+0` vtable, `+4` status:
0 ok, 1 open failed, 2 short read, `+8` handle word, `+0Ah` opened flag).
`0x45ee00` is the install-root path builder (static buffer `0x4d285c`).
Registers at a call are given where they are not the previous call's
results; f is the original's EBP.

- **prefs_video `0x45e510`(mode).** Locals: 8 bytes at f-8. If the done
  byte `0x49f254` is 0: `0x45ee00("data\prefs.cfg" 49F287h)` (ECX after:
  49F287h, the pop), `_fopen(path, "rb" 49F296h)`; a non-null FILE:
  `_fread(f-8, 1, 7, fp)` with EAX = f-8; a result of exactly 7 replaces
  the mode byte by file byte 4 and stores file byte 6 zero-extended into
  `0x4d255e` (EAX = that byte); `_fclose(fp)` (ECX after: fp); then mode =
  (mode byte == 1) ? 0 : 3 and the done byte := 1. With the done byte set
  the argument's low byte is the mode directly. Mode 0: `0x4d2537` := 0,
  `0x4d256c` := 0, word `0x4d2566` := 1, dwords `0x4d253e..0x4d2552` :=
  140h, F0h, 280h, 1E0h, 140h, 1E0h, EAX := 0, words `0x4d253c`,
  `0x4d253a` := 0. Mode 1: 3, 0, 1, 280h, 1E0h, 500h, 3C0h, 280h, 3C0h,
  both words 1, DX := 1 (EDX's high word kept). Any other mode: as mode 1
  but `0x4d256c` := 1 and AX := 1 instead of DX. Results: EAX/EDX as just
  described (otherwise the last call's, or the entry values when no call
  ran); ECX = the dword f-4 (bytes 4..6 of the read and the never-written
  byte f-1: stack residue; no caller reads ECX, both sites `pop ecx`).
- **prefs_load `0x459770`(flag).** `0x49630c` with EAX = 49E92Ch (record
  f-2Ch, context word f-1Ch := 8); read stream S = f-0Ch:
  `0x47ad40(S, 0x49e81c)` (EDX = S; `0x49e81c` is the prefs path, `data\`
  or, in playback, `tapes\prefs.cfg`); `_memset(0x4d1f38, 0, 36h)`; if S's
  status is 0, `0x47af98(S, 36h, 0x4d1f38)` (ECX = S) reads straight into
  the block (a short read leaves the rest zero). Then for i = 0..35h, with
  EAX = 0 before the first: if the callback dword `0x4d1fdc+4i` is not 0,
  it is called (read again for the call) with EAX = &block[i] and one
  argument, EDX with block[i] in its low byte (the pushed dword is the
  whole EDX; ECX after = that dword); if the flag's low byte is not 0, the
  committed copy `0x4d1f6e+i` := block[i] (read again after the callback;
  ECX = &block[i], AL = the byte); the previous copy `0x4d1fa4+i` :=
  committed[i] (DL). `0x47ad78(S, 2)` (ECX = S). Result EAX = the record's
  saved FS:0 head (written back to FS:0), EDX/ECX the destructor's. The
  mission load registers five callbacks before the call (`0x45a1b8`:
  indices 0, 1, 2, 0Eh, 8).
- **prefs_store `0x459b94`(count, list).** Frame: buffer f-70h..f-3Bh,
  record f-38h (EAX = 49E980h, context word f-28h := 8), write stream W =
  f-18h, read stream S = f-0Ch. `0x47ad40(S, 0x49e81c)` (EDX = S); if S's
  status is 0: `0x47af98(S, 36h, f-70h)` (EAX = S, ECX = f-70h), then for
  i = 0 while i < sx16(count) (signed; EDX = sx16(count) at every test):
  EDX = sx16(list[i]) (words), CL := byte `0x4d1f38+EDX`, stored at
  f-70h+EDX. The context word := 8 again; `0x47b064(W, 0x49e81c)` (EAX =
  W), if W's status is 0 `0x47b2b8(W, 36h, f-70h)` (EAX = W, ECX = f-70h);
  `0x47b09c(W, 2)` (EDX = W), `0x47ad78(S, 2)` (ECX = S). EAX = the saved
  head, EDX/ECX the last destructor's. **Stack residue:** when the read
  stream does not open or the file is shorter than 54 bytes, the buffer's
  unread bytes are the stack residue of the original's frame, and those
  are written to the file (a missing prefs file becomes 54 bytes of stack
  garbage overlaid with the listed bytes).
- **keyjoy_load `0x45b7a8`().** The string table `0x49ebe0`: path
  `0x45ee00(+14h "data\keyjoy.cfg")` (EAX = +14h at the call, ECX after =
  +14h), then for (section, key, default, "Reverse") = (+24h, +2Bh, +30h,
  +31h), (+39h, +40h, +49h, +4Ah), (+52h, +59h, +61h, +62h), (+6Ah, +71h,
  +78h, +79h): `GetPrivateProfileStringA(section, key, default, buf, 28h,
  path)` with EAX = section, EDX = default, ECX = key; `_stricmp(buf,
  "Reverse")` with EDX = "Reverse", ECX = buf; a result of 0 sets the dword
  `0x49ea90`, `0x49ea94`, `0x49ea98`, `0x49ea9c` (Tilt, Backturn, Missile,
  Rudder) to 1; nothing clears them. The 40-byte buffer is on the stack.
  Results: the last `_stricmp`'s EAX/EDX/ECX.
- **sound_config `0x46379c`(O).** EAX = EDX = ECX = 0 first; O+34h, +38h,
  +40h, +3Ch := 0, O+20h := 1, O+1Ch, O+44h := 0, words O+26h := 1, +2Eh,
  +2Ch := 0, +32h := 200h. The table `0x49fa4b`: `0x45ee00(+0
  "data\sound.cfg")` (ECX after = +0); Driver: `GetPrivateProfileStringA(
  +0Fh, +15h, +1Ch, f-28h, 28h, path)` (EAX = +0Fh, EDX = +1Ch, ECX = +15h),
  `_stricmp(buf, +1Dh "DirectSound")` (EDX = +1Dh, ECX = buf): 0 gives word
  O+2Ah := 2, else 1. Buffers: (+29h, +2Fh, +37h; EAX = +29h, EDX = +37h,
  ECX = +2Fh), `_atol(buf)` (EDX = buf; ECX after = buf): 1..64 (signed)
  gives O+30h := the value, else 5. Rate: (+38h, +3Eh, +43h; ECX = +38h,
  EAX = +43h, EDX = +3Eh), `_atol(buf)` (EAX = buf): 11 gives O+24h :=
  10h, else 20h. Width: (+44h, +4Ah, +50h; EDX = +44h, EAX = +4Ah, ECX =
  +50h), `_atol(buf)` (ECX = buf; result unused), `_stricmp(buf, +51h
  "Mono")` (EAX = +51h, EDX = ECX = buf): 0 gives O+28h := 4, else 8.
  Then ECX = `[0x6cbee4]`, `++[ECX]` (construction counter). EAX = O, EDX
  = the last `_stricmp`'s.
- **tape_save `0x45cca4`(tape, path).** Frame: length f-4, buffer
  f-1F8h (500 bytes). `_fopen(path, "rb" 49EFA8h)` (EAX = path); a FILE:
  `_fseek(fp, 0, 2)`, length := `_ftell(fp)` (ECX after = fp),
  `_rewind(fp)` (ECX after = fp); none: length := 0 (EAX = 0).
  `_fwrite(f-4, 4, 1, tape)` (EDX = tape, ECX = f-4). While the length is
  not 0: n = length >= 500 (signed) ? 500 : length; `_fread(f-1F8h, 1, n,
  fp)` (EAX = buffer), `_fwrite(f-1F8h, 1, n, tape)` (EDX = tape, ECX =
  buffer), length -= n. Neither result is tested. `_fclose(fp)` (also for
  NULL; ECX after = fp). EAX/EDX the last call's.
- **tape_restore `0x45cd5c`(tape, path).** A non-null path:
  `_fopen(path, "wb" 49EFABh)` (EAX = path); NULL: FILE = 0, no open.
  `_fread(f-4, 1, 4, tape)` (EAX = f-4; result not tested). While the
  length is not 0: n as above, `_fread(f-1F8h, 1, n, tape)` (EAX =
  buffer), with a FILE `_fwrite(f-1F8h, 1, n, fp)` (EDX = buffer), length
  -= n. `_fclose(fp)`; ECX after = fp. **Stack residue:** a tape that ends
  inside a length or a piece leaves the unread bytes as stack residue (the
  length's, or the buffer's from the previous piece or the stack), and
  both are used (the length as a loop count, the piece as file bytes).

### The joystick defaults

`0x477674` sets bit 0 of the device object when device 0 **answers**
(`0x4776fa`: `cmp [0x6bb6a0], 0` / `jne` over `or [eax], 1`; `0x6bb6a0` is
the joyGetDevCapsA result, replaced by joyGetPosEx's when the caps call
succeeded), and `0x477714` clears it. `0x459df0` therefore writes the
default mapping bytes 0Ch..24h and calls `prefs_store(25, 0x49e9a8)`
(indices 12..36) only when prefs byte 0Ch is 0 **and** a joystick answers
(`0x459fac`: `test byte [esi], 1` / `jne 0x459ff0`). Byte 0Ch is 1 in the
file afterwards, so the next mission load skips the defaults (in the oracle
the runner restores the file around every suite;
[oracle_suites.md](../reference-game/oracle_suites.md#scripted-joystick)).

### What the replacement does for the configuration hooks

Every call above is made through the weapons guest bridge with the
original's EAX/EDX/ECX: the CW3220 functions through DBSIM's thunks, so the
runtime file model (`proxy/rtl_io_shadow.h`, `core/src/rtl_io.c`) runs below
these hooks in whatever mode the `[rtl]` section selects, and the stream
classes, `0x45ee00`, the prefs callbacks and `0x49630c` stay original game
code. The original's frame lives in a zeroed 200h-byte scratch block of
the adapter at the original's EBP offsets (synthetic EBP = scratch +
1F8h): buffers, stream objects and the `0x49630c` record (prefs_load and
prefs_store call it with that EBP; the adapter writes the returned head to
FS:0). Results: EDX:EAX exact; ECX is not returned (the pair ABI; no
direct call site, and no caller up the chains above, reads it before a
write).

Not reproduced, by design (the same class as the VOL stream's stack
residue): the stack below the caller's ESP afterwards, and the residue
bytes the original takes from it, which the replacement takes from its
zeroed scratch instead: prefs_store's buffer when the prefs file is missing
or shorter than 54 bytes, tape_save's buffer after a short `_fread` (a file
that shrinks between `_ftell` and `_fread`), tape_restore's length and
buffer at a truncated tape, prefs_video's ECX. These cases count
`hk_<hook>_residue` (prefs_store, tape_save, tape_restore). Not modeled:
a prefs_store index outside 0..6Fh (the original stores it outside its
frame: below its ESP, or over its saved EBX/ESI, saved EBP, return address
and arguments; indices 36h..6Fh land in the frame and are reproduced
there, including the record and the stream objects), and a negative tape
length (the original passes it to `_fread` as a size above 2 GiB into its
500-byte buffer); both skip the store or the piece loop and count
`hk_<hook>_unmodeled`. The callers pass constant index lists (0..3, 7..11;
12..36; the set byte `0x4d2577` - 1 .. + 11; 6), and no suite has a short
or negative file.

**Compare mode:** refused by all seven (`compare_unsupported`): every one
calls game code and the runtime, and prefs_store, tape_save and
tape_restore write files a second phase would write again; the readers
read files and would call the prefs callbacks a second time. The in-vitro
comparison below is the byte-level check (bytes written and read
identical).

Callee counters (fields `hk_<hook>_<suffix>` while the hook is not
original): `call_<va>` per callee as in the contracts (prefs_video:
45ee00, 496802, 49684a, 496838; prefs_load: 49630c, 47ad40, 49681a,
47af98, `callback`, 47ad78; prefs_store: 49630c, 47ad40, 47af98, 47b064,
47b2b8, 47b09c, 47ad78, `residue`, `unmodeled`; keyjoy_load: 45ee00,
496910, 496868; sound_config: 45ee00, 496910, 496868, 496880; tape_save:
496802, 49682c, 4967de, 496772, 496832, 49684a, 496838, `residue`,
`unmodeled`; tape_restore: 496802, 49684a, 496832, 496838, `residue`,
`unmodeled`).

### Tape layout in formats/cfg

`formats/cfg` holds the tape layout: `ES2_TAPE_MEMBERS` (7), `ES2_TAPE_PIECE`
(500), `es2_tape_member_name`, `es2_tape_member_in_tapes`,
`es2_tape_header_parse` (seven u32-length records) and
`es2_tape_entry_parse` (one `control_record` entry: the 8-byte joystick
block in the first entry only, `0x49ebbc` starts as 1; the 16-byte record,
u32 N, u32 M, N 14-byte events, M key words);
[cfg.md](../formats/cfg.md#tape-files). `//formats:str_cfg_real_test`
parses the three CD tapes exactly to their end: DEMO1 header 16,313 bytes
(13,520/151/2,000/54/42/0/518), 1,499 entries; DEMO2 16,657 bytes
(13,520/495/…), 1,901 entries; DEMO3 16,305 bytes (13,520/143/…), 3,660
entries. The core takes the prefs size and the piece size from there.

### In vitro of the configuration hooks

`invitro/config_cases.h` (quick tier 100,000 direct cases per hook, full
tier 1,001,024). Direct cases run the original (through an own caller that
zero-fills the 4 KiB below its frame, so the original's residue equals the
adapter's zeroed scratch, and returns EAX/EDX/ECX), the adapter and the
core against logging stubs for every callee except `0x49630c` (real code):
`0x45ee00`, the six stream functions and the eleven thunks, plus four
prefs-callback stubs in the table `0x4d1fdc`. The stubs deliver random
bytes, short reads, failed opens and stream errors, values from a table of
INI strings ("Reverse" in several spellings, "DirectSound", numbers around
1, 11 and 64, "Mono", 39-character values) and change block bytes from
inside a callback. Compared per case: the pair, ECX (core against the
original), the ordered calls with the slot count, arguments and entry
registers (frame addresses relative to the run's first frame address),
the stub's EAX, the bytes each write receives (FNV hash and first 32
bytes) and the strings `_stricmp` and `_atol` read, the arena, the global
windows (prefs blocks, callbacks, display globals, keyjoy flags, done
byte, prefs path, construction counter), FS:0 after the call, and the
callee-counter deltas including `residue` and `unmodeled`. Then 256
installed 31/a7 poison cases per hook (fast and preserving dispatch,
stubs without failures). Then a **model pass**: the stubs become a small
file system (the stream classes and the f-functions on named files),
`GetPrivateProfileStringA` reads it through `es2_cfg_ini_get`, and
`_stricmp`/`_atol` run in the privately mapped CW3220.DLL; the original
and the installed hook run on the CD's `DATA\PREFS.CFG`, `SOUND.CFG`,
`KEYJOY.CFG` and `TAPES\DEMO1..3.TAP` and on variants (short, long,
missing, edited bytes; INI values in other cases, with blanks, quotes,
signs, out of range, missing sections) and must leave the same files,
state and pair; the original's results are then checked against
formats/cfg: the block against `es2_prefs_load`, the written prefs file
against `es2_prefs_overlay` for the three real index lists (9, 25 and 1
indices; the 25-index list is the joystick case of `0x459df0`, the
single index 6 the exit case), the display globals against
`es2_prefs_video_read`/`es2_prefs_video_mode`, the flags against
`es2_cfg_keyjoy_parse`, the sound fields against `es2_cfg_sound_parse`
(DBSIM rules), the tape written by seven tape_save calls against
`es2_tape_header_parse` (one member missing), and the seven files
tape_restore writes from each CD tape against the parsed members (with a
NULL path skipping one). All 32 branch sites of the seven functions both
ways (`branch_sites.h`, no PROVED). One caught mutation per hook
(`check_mutations.ps1`, MUTATIONS.md). `//core:config_test` checks the
core on a synthetic guest.

### Live results

prefs_video, tape_save and tape_restore run before the arming point
`0x45f42b` and stay at 0 in every suite (including `c1_01-tape-record`: its
tape header is written by the original before arming; the per-frame entries
are `control_record`'s); they are covered in vitro only. With every hook
replaced and early arming, all suites are identical to their goldens:
`prefs_load`, `keyjoy_load` and
`sound_config` run once per mission load; `prefs_store` runs once in
`c4_08-joystick` (the joystick mapping at load, the restored prefs file has
byte 0Ch = 0) and in no other suite, so no exit record shows a store at
exit; every `_residue`, `_unmodeled`, `_skipped` and `_bypassed` of the
group is 0, and `PREFS.CFG` ends with the same bytes as with the original
(the runner's restore around every suite).

### Open points of the configuration hooks

- `prefs_store` at exit (`0x45f4e1`: when the display byte 6 differs from
  the mode the run used) and in `c1_01-options-dialog` (closing the options
  screen through `0x4574e8`) is not observed live: the options suite stops
  before an exit record.
- prefs_video and the tape hooks are not reachable live with the current
  arming.
- The residue cases are counted, not reproduced.

## The stream slots and the class lookup as rows

These are the leaf slots of three stream classes, the write class's error
leaf, and the class lookup. All are service rows in
`sim/dispatch/services/file_streams.h`, with their code at the end of
`sim/src/services_files.c`. A slot whose body is byte for byte the
read-from-handle class's reuses that class's service from `runtime_leaves.h`.

| VA | Class, slot | Row | Contract (listing) |
| --- | --- | --- | --- |
| 473510 | volume `0x4a0e10`, +0xc | `vol_map` | `(S, k)`; see below (`0x473510..0x473575`) |
| 473578 | volume, +0x10 | `vol_true` | AL = 1 |
| 4735a4 | volume, +0x14 | `vol_fail` | status `[S+4]` = 4, EAX 0 |
| 47b0d8 | write `0x4a1760`, +0xc | `write_map` | `(S, k)`: 3, 1, 0, 1, 0 for k = 0..4, else 0 (jump table `0x47b0ea`); EDX, ECX stay |
| 47b1e4 | write, +4 | `write_tell` | `_lseek(sx16 [S+8], 0, 1)`; the exit registers are lseek's |
| 47b200 | write, +8 | `write_seek` | as `0x47aecc`: status 2 is cleared; another non-zero status gives 0; `_lseek(h, pos, 0)`; -1 gives `0x47b294(S)`, EAX 0, ECX = S; otherwise AL = 1 over EAX = result + 1 |
| 47b28c | write, +0x10 | `write_true` | AL = 1 |
| 47b294 | write, error leaf | `write_error` | status 1 when `[0x4967ae]` != 0, else 3; EAX 0 |
| 47b2f8 | write, +0x18 | `write_fail` | status = 4, EAX 0 |
| 47b3d4 | memory `0x4a1788`, +0xc | `memory_map` | `(S, k)`: 4 for k = 0, 1 for k = 1..3, else 0 |
| 47a4a0 | class lookup | `class_lookup` | `(tag)`; see below (`0x47a4a0..0x47a4e3`) |

`0x473510(S, k)` reads the handle word `[S+8]`; 0xffff means none. EDX
ends at -1 for k = 0, at 0 for k = 1, and at k - 4 otherwise, except
k = 4 without a handle (below). ECX stays. EAX:

| k | With a handle | Without |
| --- | --- | --- |
| 0 | 2 | dword `[S+0x1e]` |
| 1 | 0 | 0 |
| 2, 3 | 1 | 1 |
| 4 | 0 | `[S+0x1e]` != 2, in both EAX and EDX |
| other | 0 | 0 |

`0x47a4a0(tag)` searches `[0x4a1690]` pairs (table, count) at `0x6c5834`.
Each table holds 12-byte entries whose first dword is the tag.

- Found: EAX = ECX = EDX = the entry.
- Not found: EAX = 0. ECX is the address of the pair after the last.
  EDX is the end of the last table, or as at the call when there is no
  pair.

The status 3 of `0x47b294` is unreachable. Its probe `0x4967ae` is the
code of an import thunk, which is never 0 (rtl_imports.md section 4.5).

### Reachability

The slots are called through generic slot calls that take the stream
object from guest data:

| Slot | Own callers |
| --- | --- |
| +4, +8 | the stream calls in `load.c` (`vcall`) and the buffered refill on its inner stream (`services_files.c`) |
| +0x10 | the detach thunk `0x47b5cc`, on the write class's own table |
| +0x14 | `h524_stream_put` and `rd09_stream_slot` in `gfx.c`, and the stream thunks in `fill.c` |
| +0x18 | `load.c`, `fill.c`, `audio.c` and the buffered inner read |

The class is not fixed in the code, so a row is the dispatch's answer for
any such call. Two rows have no own caller in `sim/src` (static reading of
every slot use):

- the +0xc slots (473510, 47b0d8, 47b3d4). `load.c` calls +0xc only on
  newly built persistent objects.
- the volume class's +0x10 (473578).

These are rows like the read class's +0xc `0x47adb4`, which `runtime_leaves.h`
also has. The live map has 0 hits for every slot.

The class lookup has 40/40 live hits. They come from the load paths
`0x47a177` and `0x47a337`, whose own code (`load.c`, `class_lookup`)
contains the same search. The row serves the dispatch.

### Refusals

- **`0x47a594`**, slot +0x14 of the persistent-object vtable `0x49d984`
  and the base of the 13 R3D dumps:
  - It loads `edi = 0x496886`, the `__streams` thunk, as the FILE array.
  - With an indent above 0, its first instruction in the loop, `inc dword
    [edi+0x20]` at `0x47a5a7`, writes into the code section, which is
    not writable.
  - With indent 0, the same `inc` at `0x47a613` faults after `_printf`
    (`0x4a16c4`), the class lookup and, when found, a second `_printf`.
  - So the original always faults (rtl_imports.md section 4.5).
  - No own code calls +0x14 on a persistent object. `load.c` calls only
    +0xc on those, and every own +0x14 call is a stream write.
  - The live map has 0 hits.
- **`0x4967d2` (`_printf`) and `0x4967f6` (`__fputc`)**, the RTL thunks
  behind `0x47a594`:
  - All 51 of their call sites are object dumps (`0x47a5xx`,
    `0x48e...0x492xxx`), and the dumps fault before or at their first
    stream access (as above).
  - rtl_imports.md lists them as unreachable stubs.

The R3D dumps' own behind functions (`0x47a1ac`, `0x47a55c`, `0x47a638`,
`0x47a798`, `0x47c2xx..0x47c570`) are not covered here.

### In vitro

`//tools/reference-game/invitro:differential_file_streams_test`
(`file_streams_cases.h`) runs 3,000 cases with 1..8 calls each. Every row
is called 1,100 to 1,300 times, each call with random EAX, EDX and ECX.

- **Objects.** Four stream objects, each with:
  - a status 0..5;
  - a handle word, often 0xffff;
  - a dword at +0x1e, often 2.
- **Registry.** 0..3 pairs with tables of 0..4 entries, some tags
  repeated.
- **Arguments.** k is 0..6 or a random dword. The tag is a registry tag
  or a random one.
- **Stubs.** `_lseek` is a recording stub on both sides and returns -1 in
  a fifth of the calls.
- **Compared.** EAX, EDX and ECX, DATA, the heap and the stub log are
  identical.
- **Coverage.** Every path counter is non-zero:
  - lookup: found, missing, no pairs, an empty table;
  - every k class of `0x473510` with and without a handle, including k = 4
    with `[S+0x1e]` = 2 and != 2;
  - the k classes of both maps;
  - write seek: status 2, another status, lseek -1 and success.

A mutation that dropped the handle test from `0x473510`'s k = 4 branch
failed the suite (case 21, EAX and EDX differ).
