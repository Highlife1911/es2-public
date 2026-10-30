# STR packed string files

Status: **confirmed** on the reference CD corpus and the three reference
executables ([reference_version.md](../reference_version.md)). Own C17
reader and writer:
[`formats/include/es2/formats/str.h`](../../formats/include/es2/formats/str.h),
[`formats/src/str.c`](../../formats/src/str.c); the launcher
([launcher.md](../reverse-engineering/launcher.md)) and
[mission.md](mission.md#missionstr) use this grammar.

## Layout (confirmed)

All integers are little-endian. A file is

| Offset | Width | Meaning |
|---|---|---|
| 0 | u32 | `B`, the body size in bytes |
| 4 | `B` | the body: groups until it is used up |

A **group** is an `i16 N` (never negative) followed by `N` strings. A
**string** is

| Field | Width | Meaning |
|---|---|---|
| `L` | i16 | length of the text including its terminating NUL (`L >= 1`) |
| text | `L` | the characters, the last byte is the NUL, no other NUL |
| `E` | u8 | see below |
| aux | `E` | auxiliary bytes, present only for readers that ask for them |

The number of groups is not stored; the readers know how many groups they
want. A group with `N = 0` is two bytes. There is no index and no string
numbering: a string is addressed as (group, ordinal). Text encoding is the
original 8-bit code page and is not interpreted here.

**The byte `E`.** The game's string iterator (DBSIM `0x453918`, VSHELL
`0x428653`, ES.EXE `0x401824`: the same code three times) is called with an
output pointer for the text and an optional second pointer. The text pointer
is the first character. Without the second pointer the iterator moves on by
exactly `L + 1` bytes after the length word, i.e. `E` is a plain byte. With
the second pointer the iterator stores the address just after `E` and
additionally skips `E` bytes (the byte is sign-extended, so values above 127
would move backwards). Both behaviours are on the shipped files:

- Files read **with** the pointer: `system.str`, `command0..4.str`,
  `pilot0..4.str`, `sounds.str` (DBSIM `0x435a17`, `0x462570`) and group 0
  of `strings0.str` (18 strings, `0x4375bb` passes the pointer array
  `0x4d12f0`). Their strings carry auxiliary data (1,216 strings in the
  corpus; the first byte of the data is used as a table index at `0x435a3f`).
  These files only frame correctly when `E` is read as a length.
- Files read **without** the pointer: `error.str` and its localized copies
  (ES.EXE), `mission.str`, the alert files, `maplabel.str`, `pilots.str`,
  `demolist.str`. Their `E` is always 0.
- VSHELL's own streams: `campaign.str` is read by a custom routine
  (`0x40fd2d`..`0x40ff50`: `u32`, `i16 N`, then per string `L`, either read or
  skip the text, `E`, skip `E` bytes: aux semantics). `gamefile.str` is read
  and written by custom code (`0x40e380`, `0x40e503`) that treats `E` as a
  **slot flag** in the second group (`0x482751 + 0x5e*i`); a flag of 1 must
  not be read as a length. In every file of the corpus `E` is 0 where the
  game writes a flag.

The library keeps both views: without flags `E` is the aux length and the
bytes are exposed as `aux`; with `ES2_STR_TAG_BYTE` the byte is `tag` and no
`E` bytes are skipped.

**Validation by the game.** None to speak of: the iterator checks only that
the cursor is below the end of the body, never that a count, length or `E`
fits, and never that the text ends in NUL. VSHELL's group iterator
(`0x4286e2`) asserts `count <= max` when its `max` argument is positive; DBSIM's
(`0x4539a0`) ignores `max`, so a group larger than the caller's array
overruns it (the German `error.str` has 12 strings in group 0 for a six-slot
array in ES.EXE). An empty group cannot be stepped over by the group
iterator (it reads the count without consuming it). The library is stricter
on purpose: `L >= 1`, a terminating NUL, counts and lengths inside the
body, no negative count, `E <= 127` when auxiliary data is read.

## Size field and stale tails

The size field counts the bytes after it **as the writer chose**:

- `mission.str` (VSHELL `0x418014`): a placeholder 9999 is written, then the
  strings (`strlen + 1`, `E = 0`), then the placeholder is replaced by the
  bytes written minus 4 (`0x418130..0x41815d`). The stream is opened without
  truncation, so a shorter new file leaves the tail of an older one behind
  the body (stale bytes after `4 + B`; `formats` reports them as
  `trailing`).
- `gamefile.str` (VSHELL `0x40e503`): the size is the **file length** minus
  4 (`0x40e66d..0x40e69f`), so a stale tail is counted inside `B` and the two
  groups end before the end of the body. Parse such files with
  `max_groups = 2`; the body bytes after `groups_end` are the stale tail
  (see [mission.md](mission.md#sav-relationship-scope-and-exception)). The
  full parse (`max_groups = 0`) refuses them, as the groups would continue
  into the tail.

## Readers in the executables

| Reader | Address | Notes |
|---|---|---|
| DBSIM load | `0x453878(obj, path)`, body part `0x4538d0` | read stream `0x472f14` (VOL-aware); `u32` into `obj+8`, allocation, body into `obj+0`, cursor `obj+4` |
| DBSIM iterator | `0x453918(obj, &text, &aux_or_null)` | `obj+0xc` strings left in the group (0 = read the next count), `obj+0xe` ordinal |
| DBSIM group | `0x4539a0(obj, text_array, max, aux_array_or_null)` | peeks the count, calls the iterator `count` times, returns the count; `max` unused |
| DBSIM helpers | `0x4539f4`, `0x453a10`, `0x453a4c` | first-string pointer, next-string pointer (text + `L` + 1 + `E` + 2), a no-op test |
| VSHELL load / iterator / group | `0x428521` (`0x4285a7`), `0x428653`, `0x4286e2` | same code; the group routine asserts `count <= max` for `max > 0` |
| ES.EXE | `0x401758`/`0x401824`/`0x4018ac` | `error.str`, groups 0 and 1, no aux pointer |

Callers by file (path literal or extension in the executable; VOL members
are found through the VOL-aware stream):

| File | Reader | Groups requested |
|---|---|---|
| `error.str` (root) | ES.EXE `0x4018ac`; DBSIM `0x45f2d3` (`0x45f2ee`, `0x45f305`); VSHELL `0x401b53` (`0x401b6d`, `0x401b83`) | 12 and 3 (max); DBSIM and VSHELL assert on failure |
| `data\mission.str` | DBSIM `0x4245f2` (group call `0x424608`, `max` 50, strips a final LF); VSHELL `0x4135cc` (group call `0x41363b`), `0x413a07` (`0x413a76`), `0x424654` (`0x42466e`), all with `max` 0 | 1 |
| `data\maplabel.str` | VSHELL `0x424689` (group calls `0x4246a3`, `0x4246c4`, `max` 0) | 2 |
| `sav\gamefile.str` | VSHELL `0x40e380` (read), `0x40e503` (write) | custom streams |
| `<lang>\campaign.str` | VSHELL `0x40fd2d` | custom stream, one string by index |
| `st<lang>\gnl_alrt.str` etc. | DBSIM alert screens `0x455950` (gnl: groups 20/40/80, loads `0x455b60`), `0x4561dc` (gnl), `0x4566e0` (prf), `0x457538` (obj), `0x457d38` (ctl) | 3 / 1 / 2 / 1 |
| `system`, `commandX`, `pilotX` | DBSIM `0x435a17` (iterator with aux, `X` appended) | to the end |
| `strings0` | DBSIM `0x4375a6`: 41 group calls `0x4375bb..0x43788b`, group 0 (18 strings) with the aux array | 41 |
| `pilots` | DBSIM `0x4499cf` (name through `0x492bec`); the consumer after the load was not traced | 1 (36 strings) |
| `sounds` | DBSIM `0x462570`, iterator loop to the end of the file, aux pointer per string | 1 (57 strings) |
| `tapes\demolist.str` | DBSIM `0x45cf27` (`0x4538d0` directly, group call `0x45cf3d`, up to 20) | 1 |

## Corpus (confirmed, `ES2_DATA_ROOT`)

68 files: 14 loose (`ERROR.STR`, `ENGLISH|FRENCH|GERMAN\ERROR|JCONFIG|MISSION.xxx`,
`SAV\GAMEFILE.STR`, `DATA\MAPLABEL.STR`, `TAPES\DEMOLIST.STR`) and 54 VOL members
(`CAMPAIGN.STR` in `LANG0` and `SHELL0`, the `*_ALRT.STR` files in `SIMALERT`, the
system/command/pilot/sounds/strings0/pilots/demolist files in `SIMLANG` and
`SIMVOL0`). Every one frames exactly: the body is the file minus four bytes and
the groups use it completely, with 281 groups and 2,978 strings, 1,216 of them
with auxiliary data. Group-count signatures (structure only, no text):
`ERROR.STR` 6+3 strings, the localized `ERROR.xxx` 12+3, `GAMEFILE.STR` 12+12,
`MAPLABEL.STR` 2+11, `GNL_ALRT.STR` 20+40+80, `CAMPAIGN.STR` one group of 5,
`PILOTS.STR` one group of 36, `*_ALRT` `OBJ` 1+1.

Not STR files despite the name or extension: the root `DEMO.STR` (four bytes of
text, `78 0d 0a 1a`), and the `MSN\*.ENG|FRE|GER` mission-language members of
`ZONES.VOL` (see [mission.md](mission.md)); the library refuses the former with
`ES2_ERR_LENGTH`.

## Library

`es2_str_parse(input, max_groups, flags, &str, &error)` validates the header and
every group (never reads beyond the input), `es2_str_cursor_*` walks groups
and strings, `es2_str_string_at` / `es2_str_group_size` give random access,
`es2_str_write(groups, n, flags, out, cap, &required, &error)` serializes like
the original writers (plus auxiliary data and, with the tag flag, slot flags).
Strings are returned as spans into the input (without the NUL; the NUL
follows in memory). `max_groups > 0` is the way the game reads: ask for the
groups you need and ignore the rest (launcher: `max_groups = 2`, tag mode).

## Verification

- Layout and flags: llvm-objdump listings of DBSIM, VSHELL and ES.EXE (the
  addresses above), read through the iterator bodies instruction by
  instruction; the aux-pointer branch (`0x453972..0x453980`) and the
  null-pointer branch checked against the 1,216 strings of the corpus.
- Real data: `//formats:str_cfg_real_test` parses every STR under
  `ES2_DATA_ROOT` (68 files, loose and VOL members), checks group-count
  signatures, text without interior NUL, the end of every group, and
  re-serializes each file with `es2_str_write`: the result equals the file
  byte for byte. It is skipped when `ES2_DATA_ROOT` is unset or empty.
- Synthetic: `//formats:str_cfg_test` builds files byte by byte (basic, empty
  group and body, every truncation point, negative count, `L = 0`, missing NUL,
  `E` 128/200/beyond the body, tag mode, stale tail, the writer limits and
  capacity behaviour) and `//launcher:launch_logic_test` covers the launcher's
  use. Mutations of the aux check, the length check, the tag flag, the writer
  size and the launcher's flag were each caught by at least one of them.

## Open

- The first aux byte of the voice/command strings indexes a table (`0x435a3f`);
  what the rest of the auxiliary data holds (lip-sync scripts per
  [audio.md](../reverse-engineering/audio.md)) is not decoded here.
- The consumer of `obj_alrt.str` and the exact use of `strings0.str` groups 1..40
  were not traced.
- Which older file left the four stale bytes in the captured `gamefile.str`
  (mission.md) is unknown.
