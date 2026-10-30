# Configuration files: prefs.cfg, sound.cfg, keyjoy.cfg

Layouts and loading rules: static, listings of the
reference executables, [reference_version.md](../reference_version.md), and
checked against the shipped files. Meanings of
single `prefs.cfg` bytes are marked: **C** from code, **H**
hypothesis. Own C17 module:
[`formats/include/es2/formats/cfg.h`](../../formats/include/es2/formats/cfg.h),
[`formats/src/cfg.c`](../../formats/src/cfg.c). Where the files are opened and
what the original does around them: [file_io.md](../reverse-engineering/file_io.md)
(sections 4.1, 4.2, 4.3, 5 and 9). `drive.cfg` and `language.cfg` are owned by
the launcher; `exit.cfg` (two zero bytes on the CD, no reader found) is not
modeled.

## prefs.cfg

A flat, headerless block of **54 bytes** (`0x36`), shared by VSHELL (block at
`0x48259c`, committed copy `0x4825d2`) and DBSIM (block `0x4d1f38`, committed
copy `0x4d1f6e`, previous-value copy `0x4d1fa4`, one callback slot per byte at
`0x4d1fdc`). The path is `data\prefs.cfg` below the install root (DBSIM keeps it in
`0x49e81c`; during tape playback `tapes\prefs.cfg`, which holds bytes 0..3 and
8..10 of the real file, file_io.md, DBSIM recording and playback).

**Loading (C).** Both programs zero the block (DBSIM memset `0x45979a..0x4597a8`,
VSHELL `0x40dc90`), open the file, and read up to 54 bytes
(VSHELL `0x40dc5b`/`0x40dca4`, DBSIM `0x459770`/`0x45979a`); a missing file or
a short read is not an error and leaves the rest zero. Bytes beyond 54 are
never read. The CD file `DATA\PREFS.CFG` is exactly 54 bytes.

**Writing (C).** Whole block: 54 bytes into a truncating stream (VSHELL
`0x40dd0a`, accept in the Preferences screen and the practice screen). Single
bytes: read the file into a 54-byte buffer, overlay the listed indices from the
block and write all 54 bytes back (VSHELL `0x40df90` → `0x40dfa3`; DBSIM
`0x459b94` with an index list; callers `0x4574e8` options screen exit,
`0x45915c` control mapping, `0x459df0` defaults, `0x45f160` at exit for byte
6). The DBSIM copy of the buffer is not initialised when the file is shorter
than 54 bytes, so stale stack bytes may follow; the library writes zeros.
VSHELL's buffer starts as a copy of the current block (`0x40dfe4`), so after
a short file the current block's bytes follow and a missing file writes the
whole current block ([vshell_ui.md](../reverse-engineering/vshell_ui.md)
section 11; `es2_shell_prefs_store`).

**Early video read (C).** DBSIM `0x45e510` (called at `0x45e72e`/`0x4653c9`,
before the block exists) reads exactly 7 bytes and keeps byte 4 and byte 6.
Byte 4 equal to 1 selects video mode 0 (first size pair 320x240, set at
`0x45e5ac`), any other value mode 3 (first size pair 640x480, `0x45e679`)
(`0x45e56d..0x45e6c4`). Byte 6 becomes the display flag `0x4d255e`. A short
read leaves the caller's default. At the end of a run DBSIM compares the
file byte 6 with the mode it actually ran in and rewrites it when they differ
(`0x45f4e1..0x45f504`).

### Byte table

Readers are given by address. DBSIM reads through immediate indices (accessors
`0x459958` set, `0x45999c` toggle, `0x4599d4`/`0x459a10` step up/down,
`0x459af4` restore) and through the callbacks registered by `0x45a1b8`
(`0x459c74(index, fn)`): index 0 `0x459cb4`, 1 `0x459c88`, 2 `0x459ce0`, 8
`0x459d68`, 0xe `0x459d3c`.

| Byte | Reader / writer | Meaning |
|---|---|---|
| `+0x00`, `+0x01` | DBSIM callbacks `0x459cb4` (→ `0x462d40`, sound start/stop `0x462e58`/`0x462d78`) and `0x459c88` (→ `0x462d5c`, `0x462efc`/`0x462ddc`); DBSIM options screen `0x456d68` toggles `+1`; VSHELL `0x42f2f4` start-up (`0x42f7ff`, `0x42f89f`, `0x42f50a..`), `0x40796c`, Preferences `0x436ed1` toggles | **C** two-state audio switches, each gating a group of sample rows ([audio.md](../reverse-engineering/audio.md)); the DBSIM options screen toggles them. On VSHELL's side (C): `+0x00` gates the music ramp (the Music toggle fades the shell's WAV music), `+0x01` gates the effects; neither is CD music. **H** music and sound effects. |
| `+0x02` | DBSIM callback `0x459ce0` (stores `value != 0` into `0x49f966`, "voice enable"), `0x435e58`, `0x436642`; the DBSIM options screen steps it modulo 3; DBSIM sets it to 0 when `vol\simvoic<lang>.vol` is absent (`0x45a22a`); VSHELL `0x436ed1` cycles 0, 1, 2 | **C** three-state speech option, nonzero enables voices. |
| `+0x03` | DBSIM `0x436ae6`, `0x436c84`; set to 0 with `+2` when the voice archive is absent | **C** speech-related, **H** text/subtitle choice. |
| `+0x04` | DBSIM `0x45e510` (see above); VSHELL Preferences `0x43714c` (called by `0x437758` with 0 and `0x4376f7` with 1) and `0x436ed1`'s redraw `0x4370b8` | **C** resolution: 1 low (320x240), otherwise high (640x480). **H** the two Preferences radio buttons "Low Res"/"High Res" (ESTEXT 0x121/0x122). The Preferences table of [vshell_ui.md](../reverse-engineering/vshell_ui.md) pairs these handlers with the Music/Sound Effects labels; the code shows the audio switches go through `0x436ed1` and the resolution through `0x43714c`, so that pairing looks swapped. |
| `+0x05` | VSHELL `0x42f40f` reads, `0x42f497` toggles | **C** the shell's music choice ([audio.md](../reverse-engineering/audio.md) "Start-up sequence and the music choice"): ≠ 0 plays `hmi\shell1.wav`, 0 `shell2.wav`; every VSHELL start with sound on flips it and writes the block back (the own shell reproduces this). |
| `+0x06` | DBSIM `0x45e559` and `0x45f4e1..0x45f504`; VSHELL `0x406b2f`, Window `0x437200` (:= 0), Full Screen accept (:= 1), `0x431e79` ONLINE MANUAL (:= 0), `0x404fb8` and `0x41e98c` (:= 0/1) | **C** display mode: 0 window, 1 full screen. |
| `+0x07..+0x0b` | DBSIM options screen `0x456d68` (modulo 3 at `+7`; 5 at `+9`; 3 at `+0xa`, `+0xb`; toggle `+8`), HUD/world readers `0x457251..0x45738f`, `0x4278cd`, `0x40393b`, `0x408dd7`, `0x45fcbb`, `0x45fcdb`, `0x461ed5..0x461f1b`, `0x4627e9`, `0x46cbcf`; callback 8 `0x459d68` stores `value != 0` into `0x4aaaa8` | **C** DBSIM option bytes (cyclic with the ranges given); roles not decoded (**H** detail levels). |
| `+0x0c` | `0x459df0` reads it (`0x459e02`, selects the default-mapping branch) and sets it to 1 (`0x45a000`) | **C** selector of the default mapping tables; **H** controller class. |
| `+0x0d..+0x18` | control mapping set A: 12 bytes (`+0x0e` inside it also has the callback `0x459d3c` and is compared with 1 at `0x4483c8`, `0x4607e9`, `0x460e4e`: with a throttle lever present its value 1 sets the lever direction word `0x49a06e`, [input.md](../reverse-engineering/input.md) action 5). `0x45a5dc` indexes `s + k` with `s = 13`, `s + 4 + b` for button `b < 8` (actions `0x4d1f49..0x4d1f50`); defaults from the tables `0x45a274` | **C** set A is used when DBSIM's selector `s` is 13 (`0x45cbd2`, `0x4617d9..0x4617de`: `s = 0x19` when the byte at `[ebx+0xb5]` is nonzero). |
| `+0x19..+0x24` | control mapping set B: 12 bytes, `s = 25` | **C** same as set A for `s = 25`. Action meanings: [input.md](../reverse-engineering/input.md) (table of action numbers 0..20). |
| `+0x25..+0x29` | VSHELL practice screen `0x44c5b9`, `0x44c676`, `0x44c733`, `0x44c7f0`, `0x44c8ad`; preparation `0x41cd61`, `0x41cbb1` | **C** mode-0 controls: ammunition (limited/unlimited), damage, difficulty (mod 4), Herc type (mod 9), time of day; [vshell_ui.md](../reverse-engineering/vshell_ui.md) 4.4, [vshell_preparation.md](../reverse-engineering/vshell_preparation.md#shell-options). |
| `+0x2a` | VSHELL `0x40e753` (start-up copy into the mode word `0x4826f0`), writer `0x40ec56` | **C** shell mode: 0 single missions, 1 campaign. |
| `+0x2b` | VSHELL `0x4079b7` (copied into `0x48235e` by the start-up record built in `0x40796c`) | **C** read; role **H** (display/sound start-up parameter). |
| `+0x2c` | VSHELL `0x40edbc`, `0x4326c7` repair bay, Preferences `0x43712c` | **C** repair option 0 auto all, 1 manual (own Herc), 2 manual all. |
| `+0x2d` | VSHELL `0x40f09f`, `0x412930`, `0x4499b9`, `0x449d2f`, `0x44a06e`, Preferences `0x4371e0` | **C** weapons-building option 0 auto, 1 manual. |
| `+0x2e` | VSHELL `0x44c58b` (instant action, modulo 3) | **C** instant action rotation. |
| `+0x2f` | VSHELL `0x431890` compares with 1; `0x4318d6` sets it to 2 | **C** main-menu one-shot state (0, 1, 2); role **H**. |
| `+0x30..+0x35` | no reader or writer names them | untouched by both programs; preserve. |

DBSIM never reads `+0x2a`, `+0x2c`, `+0x2d` (verified by listing all absolute
and accessor references). The shipped `DATA\PREFS.CFG` has mode 0, repair 0,
building 0. The CD file's other values are installation defaults and are not
reproduced here.

### API

`es2_prefs_load` (the start-up rule), `es2_prefs_serialize`, `es2_prefs_overlay`
(single-byte write), `es2_prefs_video_read`/`es2_prefs_video_mode` (the
7-byte read); named offsets `ES2_PREFS_*`. The shell options with UI value
checks stay in `shell/` (`es2_shell_prefs_*`); that code and this one
describe the same block.

## INI files: sound.cfg and keyjoy.cfg

Both are read with `GetPrivateProfileStringA(section, key, "", buffer, 0x28,
path)` (IAT `0x6cc400` in DBSIM). The default is the empty string in every
call. The path is `data\sound.cfg` / `data\keyjoy.cfg` relative to the install
root, exactly as for the other `data\` files; whether Windows resolves a
relative name against the current directory or the Windows directory is
unchanged from file_io.md, Open questions (open, no live check). The shipped
files are CRLF text, a section header, `Key = Value` lines, each followed by `;;`
comment lines, and a final Ctrl-Z.

**sound.cfg**, section `[Sound]` (DBSIM `0x46379c`, reads at `0x463806`,
`0x463840`, `0x463878`, `0x4638ad`; VSHELL `0x42c591`, reads at `0x42c5fc`,
`0x42c61d`, `0x42c658`, `0x42c690`, started from `0x42f2f4`; DBSIM from
`0x462410` ← `0x461608`):

| Key | Rule (C) | Result field of the sound object |
|---|---|---|
| `Driver` | DBSIM: `_stricmp(value, "DirectSound") == 0` selects DirectSound (2), anything else including empty MME (1). VSHELL reads it and ignores the result: always 1 | `+0x2a` |
| `Buffers` | `_atol(value)`; accepted when 1..64, otherwise 5 | `+0x30` |
| `Rate` | `_atol(value) == 11` gives `0x10`, anything else `0x20` (22 is the shipped value) | `+0x24` |
| `Width` | `_stricmp(value, "Mono") == 0` gives 4, anything else 8 (the call `_atol(value)` before the compare has no effect) | `+0x28` |

The object also receives constants (`+0x20` = 1, `+0x26` = 1, `+0x2c` = 0,
`+0x2e` = 0, `+0x32` = `0x200`); the audio layer ([audio.md](../reverse-engineering/audio.md)) owns them. With no
file all four defaults are empty, so the result is MME, 5 buffers, rate code
`0x20`, width 8 (this is the answer to the "default is empty (not
DirectSound)" note in file_io.md).

**keyjoy.cfg**, section `[Keyjoy]` (DBSIM `0x45b7a8`, reads at `0x45b7d2`,
`0x45b80a`, `0x45b842`, `0x45b87a`, from `0x45a5dc` ← `0x461608`; VSHELL does not
read it): keys `Tilt`, `Backturn`, `Missile`, `Rudder`; each compared with
`Reverse` (`_stricmp`), a match sets a flag to 1, otherwise it stays 0
(the flags are never cleared). The flags are `0x49ea90` (Tilt), `0x49ea94`
(Backturn), `0x49ea98` (Missile), `0x49ea9c` (Rudder). Their use in the input
code `0x45a5dc`: Tilt negates `0x4d23cd` (`0x45aeca..0x45aed3`), Rudder
negates the dword at `[[edi]+0x19]` (`0x45aee0..0x45aeeb`), Backturn negates the
word at `[ebx+0xe]` when `[ebx+0x10] > 0` (`0x45b633..0x45b643`), and
**Missile negates `0x4d23c5` when the flag is 0** (`0x45adfb..0x45ae04`): the
default for the missile axis is the inverted sign. `Reverse` therefore
means "the opposite of the default" for all four.

### Library INI reader

`es2_cfg_ini_get(file, section, key, out, capacity, &found)` is an own subset
of `GetPrivateProfileStringA`, not a port of Windows: LF or CR LF line ends,
`;` comment lines, `[section]` headers and `key = value` lines, ASCII
case-insensitive names, the first match wins, values trimmed of blanks and of
one pair of matching quotes (documented Windows behavior; no shipped file
quotes), the result cut to `capacity - 1` characters (the game passes 40 bytes).
Not claimed: Windows' treatment of names the game never asks for, of
duplicate sections, of lines longer than the internal buffers and of a section
without header. `es2_cfg_sound_parse` and `es2_cfg_keyjoy_parse` apply the rules
above; `ES2_CFG_READER_VSHELL` models the ignored driver.

## Verification

- Listings: every address above was read in the `llvm-objdump` listings of
  `VER95\DBSIM.EXE` and `VSHELL.EXE`; imports resolved from the PE tables
  (`_stricmp`, `_atol`, `GetPrivateProfileStringA`); string literals read from
  the images (`data\sound.cfg`, `Sound`, `Driver`, `DirectSound`, `Buffers`,
  `Rate`, `Width`, `Mono`, `Keyjoy`, `Tilt`, `Backturn`, `Missile`, `Rudder`,
  `Reverse`; all defaults are empty strings). Prefs offsets: all absolute references to both blocks and all accessor calls
  with immediate indices were listed with a script (scratch, ignored).
- Real data: `//formats:str_cfg_real_test` loads the shipped `PREFS.CFG`
  (54 bytes, re-serialized identical, mode/repair/build in range), parses
  `SOUND.CFG` (all four keys present; MME, buffers in 1..64) and `KEYJOY.CFG`
  (four keys present); it also runs the INI reader over the other `.CFG`
  files of the media without failure. Skipped without `ES2_DATA_ROOT`.
- Synthetic: `//formats:str_cfg_test` (load of missing/short/long files,
  overlay, 7-byte read, INI edge cases, every value rule above with its
  boundaries, both readers).
- In vitro against DBSIM's own code: the model pass of
  `invitro/config_cases.h` runs the original `0x459770`, `0x459b94`,
  `0x45e510`, `0x45b7a8` and `0x46379c` on the CD files and on variants
  (GetPrivateProfileStringA served by `es2_cfg_ini_get`, `_stricmp` and
  `_atol` from the real CW3220.DLL) and checks the results against
  `es2_prefs_load`, `es2_prefs_overlay`, `es2_prefs_video_read`/`_mode`,
  `es2_cfg_keyjoy_parse` and `es2_cfg_sound_parse` (DBSIM): all equal. The
  INI parsing itself is the library's there, so the Windows side is still
  unverified.
- Not verified live: the behavior of the original with a modified file
  (clone experiment), the Windows name-resolution question, and the meaning of
  the **H** bytes.

## Open

- Names for `+0..+3`, `+5`, `+7..+0xb`, `+0x2b`, `+0x2f`: needs the DBSIM
  options screen (`0x456d68`) string IDs and the VSHELL Preferences control
  constructors; not traced here.
- The control-mapping default tables (`0x45a274` variants 0 and 1) and the
  meaning of selector `[ebx+0xb5]` (joystick present?) belong to the input
  documentation ([input.md](../reverse-engineering/input.md)).

## Tape files

The DBSIM side is in
[file_io.md](../reverse-engineering/file_io.md#configuration-and-tape-files-as-own-code)
(section 4.5 and the configuration and tape contracts). A `.tap` file written by `-r` and
read by `-p`/`-D`:

- **Header (C):** seven records, one per launch file, in this order:
  `script.dat`, `player.mec`, `mission.var`, `prefs.cfg`, `restore.dat`,
  `object.str`, `keyjoy.cfg`. Each record is a u32 length (0 for a
  missing file) and that many bytes. Recording reads `data\<name>`;
  playback writes `data\<name>`, except `prefs.cfg` and `keyjoy.cfg`,
  which go to `tapes\<name>`. DBSIM copies the bytes in pieces of at most
  500 (`0x45cca4`, `0x45cd5c`); the pieces do not show in the file.
- **Entries (C, `control_record` `0x45a810`):** one per call; the first
  entry starts with 8 bytes of the joystick block (`0x477904`; the flag
  `0x49ebbc` starts as 1), then every entry: the key word, four axis words,
  the time-scale word, a dword of button bits (0..7 the first button set,
  8..15 the second, 16 the mouse button), u32 N (mouse events), u32 M
  (queued keys), N 14-byte events, M key words.

API: `ES2_TAPE_MEMBERS`, `ES2_TAPE_PIECE`, `es2_tape_member_name`,
`es2_tape_member_in_tapes`, `es2_tape_header_parse`,
`es2_tape_entry_parse` (counts beyond the remaining bytes give
`ES2_ERR_TRUNCATED`).

Verification: `//formats:str_cfg_test` (synthetic header, prefix, counts,
truncation); `//formats:str_cfg_real_test` parses the CD's
`TAPES\DEMO1..3.TAP` exactly to their end (1,499, 1,901 and 3,660 entries;
headers 16,313, 16,657 and 16,305 bytes); the in-vitro model pass
checks the header against DBSIM's own writer and reader.
