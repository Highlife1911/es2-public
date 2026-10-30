# Game data inventory

Listing of the installed game files, the CD tree and every archive member
type, with the role of each file type and the format document that owns it.
**Confirmed** means measured metadata or bytes, with source paths and archive
hashes in [formats/vol.md](formats/vol.md). **Hypothesis** means a proposed
role or behaviour that still needs a format or original-loader check. Counts
are files, not directories; sizes are exact logical bytes, not disk
allocation. Extensions are counted case-insensitively.

## Installed game

**Confirmed:** the listing below is the file set of the installation
`build\installs\native-r111`, taken by a recursive listing
only (no file opened, hashed, modified or launched). Nested `GERMAN/BILLBRD`
is included in `GERMAN`; root files are counted separately. It is a used
installation, including saves, handoff files and the project's compatibility
files, not a pristine CD inventory; files the project replaced afterwards
(own shell, presenter, runtime) are not part of these counts.

| Folder (recursive) | Files | Bytes | Extension counts |
| --- | --- | --- | --- |
| `(root files)` | 17 | 1,994,414 | `.DLL` 5, `.EXE` 3, `.ICO` 1, `.INI` 2, `.JSON` 2, `.STR` 1, `.TXT` 1, `.WIN` 2 |
| `DATA` | 14 | 17,412 | `.CFG` 6, `.DAT` 4, `.MEC` 1, `.STR` 2, `.VAR` 1 |
| `GERMAN` | 7 | 9,348,093 | `.BMP` 1, `.GER` 3, `.HLP` 1, `.INF` 1, `.WRI` 1 |
| `SAV` | 17 | 64,520 | `.DAT` 4, `.MEC` 4, `.SAV` 4, `.STR` 5 |
| `TAPES` | 4 | 219,264 | `.STR` 1, `.TAP` 3 |
| `VOL` | 13 | 68,092,365 | `.VOL` 13 |
| **Total** | **72** | **79,736,068** |  |

**Confirmed names/sizes:** the 13 installed `VOL/` files match the 13 CD
`VOL/` filenames and sizes in the archive table below. Installed archive
contents were not checked; all member counts in this document are from the CD
copies. No `SIMVOICE`, `SIMVOICF`, `SIMVOICG` or movie folder is present in the
installation; video stays on the CD, see [setup.md](reference-game/setup.md#the-original-installer-and-the-portable-layout).

Root files: `ES.EXE`, `VSHELL.EXE`, `DBSIM.EXE`; the four bundled DLLs
`BWCC32`, `CW3220`, `SOS9503`, `SOSLIBS3`; and `ddraw.dll` (49,152 bytes).
**Confirmed:** the last is the project's compatibility proxy, not the CD's
DDRAW binary; `es2-oracle.ini` and the two JSON manifests also belong to the
portable setup. See [README.md](../README.md#installations) and
[media_compatibility.md](reference-game/media_compatibility.md).

### Mutable DATA and save names

**Confirmed installed listing:**

| Installed `DATA/` file | Bytes |
| --- | --- |
| `DRIVE.CFG` | 44 |
| `EXIT.CFG` | 2 |
| `KEYJOY.CFG` | 518 |
| `LANGUAGE.CFG` | 1 |
| `MAPLABEL.STR` | 195 |
| `MAT0.DAT` | 244 |
| `MFORMS.DAT` | 142 |
| `MISSION.STR` | 939 |
| `mission.var` | 2,000 |
| `player.mec` | 215 |
| `PREFS.CFG` | 54 |
| `results.dat` | 152 |
| `script.dat` | 12,286 |
| `SOUND.CFG` | 620 |

**Confirmed:** VSHELL prepares `script.dat`,
`player.mec`, `mission.var` and `MISSION.STR` for DBSIM; `results.dat` carries
the simulator's return data. The schemas, producers and readers are in
[mission.md](formats/mission.md#es-vshell-and-dbsim-file-handoff).
The sizes above are those of the listing.

**Confirmed installed save listing:** `GAME_0.SAV`, `GAME_1.SAV`, `GAME_R.SAV`,
`GAME_T.SAV`, `GAMEFILE.STR`, plus `missn{0,1,10,11}.str`,
`player{0,1,10,11}.mec`, `script{0,1,10,11}.dat`. Save/restore copying and
slot use are in [mission.md](formats/mission.md); the listing does not say
which run produced a particular save.
`TAPES/` contains `DEMO1.TAP`, `DEMO2.TAP`, `DEMO3.TAP`, `DEMOLIST.STR`.

## CD context

**Confirmed:** the read-only source tree is `build\extracted\cd`.
All 865 file hashes match [iso_file_hashes.sha256](../data/iso_file_hashes.sha256).
The CD includes assets absent from the installation and bundled demos/system
installers. These counts do not add archive members a second time.

| CD folder (recursive) | Files | Bytes |
| --- | --- | --- |
| `(root files)` | 24 | 1,543,700 |
| `AUTORUN` | 6 | 280,178 |
| `AVF` | 2 | 13,110,028 |
| `AVG` | 2 | 13,110,028 |
| `AVI` | 93 | 151,978,928 |
| `DATA` | 7 | 1,775 |
| `DEMOS` | 73 | 84,854,151 |
| `DIRECTX` | 137 | 3,826,220 |
| `DRIVERS` | 84 | 1,175,832 |
| `ENGLISH` | 7 | 9,333,083 |
| `FRENCH` | 7 | 9,355,785 |
| `GERMAN` | 7 | 9,348,093 |
| `INDEO` | 73 | 2,526,659 |
| `SAV` | 1 | 278 |
| `SIMVOICE` | 65 | 7,981,289 |
| `SIMVOICF` | 65 | 7,981,289 |
| `SIMVOICG` | 65 | 7,981,289 |
| `TAPES` | 4 | 219,264 |
| `VER31` | 19 | 26,477,151 |
| `VER95` | 4 | 1,398,368 |
| `VOL` | 13 | 68,092,365 |
| `WIN32S` | 80 | 1,881,926 |
| `WING` | 27 | 502,357 |
| **Total** | **865** | **422,960,036** |

**Confirmed:** `AVI`, `AVF`, `AVG` contain 93, 2, 2 `.AVI` files respectively.
`SIMVOICE`, `SIMVOICF`, `SIMVOICG` each contain 65 loose `.WAV` files; their
relative filenames and SHA-256 hashes are identical across all three folders.
Do not infer translated content merely from those folder names. They are
separate from the 213-member voice archives; no exact same-path loose/VOLN
member pairs exist in this CD tree.

**Confirmed layout / proposed roles:** `ENGLISH`, `FRENCH`, `GERMAN` each have
seven files (`.BMP`, three language-extension files, `.HLP`, `.INF`, `.WRI`).
The setup's language selection and resource copying are documented in
[setup.md](reference-game/setup.md#the-original-installer-and-the-portable-layout). **Hypothesis from
folder/file names:** `DIRECTX`, `DRIVERS`, `INDEO`, `WIN32S`, `WING` contain
legacy support installers; `DEMOS` contains other-game/demo assets. None is
part of the current 72-file installation listing.

## CD archives

**Confirmed:** the 16 `VOLN` archives contain 3,802 member records, including
duplicates across archives. The three `VER31` copies are different files
from their `VOL/` counterparts, not installed archives. The `VOL/`
subset has **3,163 entries** and occupies **68,092,365 bytes**. Per-file hashes,
offset validation and the eight non-`VOLN` exceptions are documented in
[formats/vol.md](formats/vol.md).

| CD archive | Bytes | Entries | Member extension counts |
| --- | --- | --- | --- |
| `VER31/SIMVOICE.VOL` | 5,226,622 | 213 | `.WAV` 213 |
| `VER31/SIMVOICF.VOL` | 5,376,471 | 213 | `.WAV` 213 |
| `VER31/SIMVOICG.VOL` | 5,050,733 | 213 | `.WAV` 213 |
| `VOL/LANG0.VOL` | 54,404 | 21 | `.BIN` 18, `.STR` 3 |
| `VOL/SHELL0.VOL` | 8,826,962 | 170 | `.DAT` 39, `.DBA` 83, `.DBM` 10, `.DFN` 4, `.DPL` 33, `.STR` 1 |
| `VOL/SHELL1.VOL` | 69,345 | 4 | `.DFN` 4 |
| `VOL/SHLSOUND.VOL` | 2,124,648 | 5 | `.WAV` 5 |
| `VOL/SIMALERT.VOL` | 748,304 | 23 | `.DBA` 5, `.DCI` 1, `.HBA` 5, `.STR` 12 |
| `VOL/SIMLANG.VOL` | 41,457 | 26 | `.STR` 26 |
| `VOL/SIMPATCH.VOL` | 72,437 | 10 | `.BND` 1, `.DFN` 2, `.HBA` 1, `.HFN` 2, `.PDG` 1, `.WAV` 3 |
| `VOL/SIMSOUND.VOL` | 2,291,346 | 641 | `.SNC` 556, `.WAV` 85 |
| `VOL/SIMVOICE.VOL` | 7,042,407 | 213 | `.WAV` 213 |
| `VOL/SIMVOICF.VOL` | 7,261,567 | 213 | `.WAV` 213 |
| `VOL/SIMVOICG.VOL` | 6,610,094 | 213 | `.WAV` 213 |
| `VOL/SIMVOL0.VOL` | 30,817,065 | 1,300 | See expanded table below. |
| `VOL/ZONES.VOL` | 2,132,329 | 324 | `.DAT` 39, `.DBA` 39, `.ENG` 62, `.FRE` 61, `.GER` 61, `.MSN` 62 |

**Confirmed `VOL/SIMVOL0.VOL` extension counts (total 1,300):**

| Extension | Count | Extension | Count |
| --- | --- | --- | --- |
| `.BND` | 83 | `.GAU` | 9 |
| `.COL` | 22 | `.GL` | 21 |
| `.DAT` | 62 | `.HB0` | 9 |
| `.DB0` | 9 | `.HB1` | 9 |
| `.DB1` | 9 | `.HB2` | 9 |
| `.DB2` | 9 | `.HBA` | 45 |
| `.DBA` | 126 | `.HD0` | 9 |
| `.DCI` | 7 | `.HD1` | 9 |
| `.DFN` | 26 | `.HD2` | 9 |
| `.DGS` | 2 | `.HD3` | 9 |
| `.DMG` | 22 | `.HFN` | 24 |
| `.DPL` | 32 | `.NAM` | 2 |
| `.DTS` | 55 | `.OFS` | 12 |
| `.ED0` | 9 | `.PDG` | 22 |
| `.ED1` | 9 | `.RMP` | 10 |
| `.ED2` | 9 | `.SNC` | 556 |
| `.ED3` | 9 | `.STR` | 13 |
| `.EDG` | 2 | `.VUE` | 9 |
| `.FM` | 2 | `.WLD` | 10 |

**Confirmed demo exceptions:** seven `VOL ` archives contain 688 members.
Counts use the used name-string region and validate one active 14-byte index
record per name, including its `VBLK` offset and payload bounds. Reserved table
capacity is not an entry count. `LOWTED.VOL` is listed explicitly to avoid
mistaking every `.VOL` suffix for a container.

| CD path | Bytes | Entries | Member extension counts |
| --- | --- | --- | --- |
| `DEMOS/A10/640.VOL` | 1,601,124 | 18 | `.BMP` 3, `.DIB` 3, `.PBA` 4, `.PFT` 8 |
| `DEMOS/A10/CMP1/LOWTED.VOL` | 16 | N/A: name list | References `.TED` and `.DML`; no embedded members. |
| `DEMOS/A10/CMP1/TED.VOL` | 2,957,940 | 83 | `.BMP` 80, `.DIB` 2, `.DML` 1 |
| `DEMOS/A10/CMP1.VOL` | 670,164 | 6 | `.BMP` 1, `.DIB` 2, `.PBA` 2, `.PPL` 1 |
| `DEMOS/A10/SHAPES.VOL` | 1,484,768 | 535 | `.BMP` 413, `.DML` 60, `.DT2` 61, `.TXL` 1 |
| `DEMOS/A10/SIM.VOL` | 8,132 | 7 | `.PPL` 7 |
| `DEMOS/A10/SOUNDS.VOL` | 993,956 | 32 | `.WAV` 32 |
| `DEMOS/A10/VOICE.VOL` | 240,064 | 7 | `.WAV` 7 |

## File-type roles and confidence

The table covers the installed file types, all archive-member types above and
loose game media. Bundled support-installer internals are only enumerated in
the CD manifest; their schemas are outside this inventory. A shared extension does not establish a shared schema.

| Type(s) | Role and evidence / confidence |
| --- | --- |
| `.VOL` | **Confirmed:** resource containers for the 16 `VOLN` and seven `VOL ` files; the named `LOWTED` exception is a list. See the format audit. |
| `.EXE`, `.DLL` | **Confirmed:** launcher, shell, simulator and runtime libraries; [reference_version.md](reference_version.md) records the original binary hashes. |
| `.WIN`, `.INI` | **Confirmed:** `HMIDRV.WIN`/`HMIMDRV.WIN` and `SOSLIB.INI` belong to the audio runtime; `es2-oracle.ini` configures our proxy. See [setup.md](reference-game/setup.md#the-original-installer-and-the-portable-layout) and [media_compatibility.md](reference-game/media_compatibility.md). |
| `.JSON` | **Confirmed:** installation/media manifests, not original game formats. |
| `.CFG` | **Confirmed:** CD/language selection via `DRIVE.CFG`, `LANGUAGE.CFG` (BATCH.EXE `0x40126f–0x401335`, owned by the launcher); `PREFS.CFG`, `SOUND.CFG`, `KEYJOY.CFG` layouts and loading in [cfg.md](formats/cfg.md). `EXIT.CFG` (two bytes) has no reader found and is not modelled. |
| `.STR`, `.BIN`, `.ENG`, `.FRE`, `.GER` | **Confirmed:** mission text, packed STR groups and localized MSN companions have distinct readers in [str.md](formats/str.md) and [mission.md](formats/mission.md). Other members remain unclassified; the extension alone does not select a schema. |
| `.DAT` | **Confirmed:** simulator and shell configuration families, with separate path-selected layouts; 142 inputs / 67,539 bytes audited. [dat.md](formats/dat.md) distinguishes original-reader layouts, candidate-only fits, external counts and unread suffixes; the Herc picture tables are in [herc_pictures.md](formats/herc_pictures.md). Mission `script.dat` is in [mission.md](formats/mission.md). |
| `.DBA`, `.HBA` | **Confirmed:** indexed bitmap arrays, including resolution variants; 252 DBA and 51 HBA strict parses, one documented CORNERS.DBA suffix exception. See [dbm.md](formats/dbm.md). |
| `.DBM` | **Confirmed:** ten individual indexed bitmaps; layout and compression support in [dbm.md](formats/dbm.md). |
| `.DPL` | **Confirmed:** 65 palette resources with color and ramp tables; [dpl.md](formats/dpl.md). |
| `.DFN`, `.HFN` | **Confirmed:** all 36 DFN and 26 HFN payloads start `05 00 28 00`; names include `FONT`, `GREEN6X8`, `TITLE`; bitmap fonts (`GLFont`), layout in [dfn.md](formats/dfn.md). **Hypothesis:** HFN is the high-resolution variant. |
| `.DTS` | **Confirmed:** 55 model/animation resources, 479 roots and 52,966 objects; full class framing and dependency evidence in [dts.md](formats/dts.md). |
| `.MSN` | **Confirmed:** version-5 mission setup, actors and trigger/action records; 61 complete parses and one truncated DEMO2 member. See [mission.md](formats/mission.md). |
| `.MEC`, `.VAR`, `.SAV`, `.TAP` | **Confirmed:** mission handoff, campaign-save framing and save/restore paths in [mission.md](formats/mission.md), including `script.dat` and `MISSION.STR`. Tape layout and some saved-field meanings remain **open**; shared suffixes do not imply the handoff schema. |
| `.WAV` | **Confirmed for 1,371 VOLN members:** raw `RIFF`/`WAVE`, PCM mono, 11,025 or 22,050 Hz; 1,364 are 8-bit, seven 16-bit. CD loose copies and demo members are inventoried separately. |
| `.AVI` | **Confirmed:** intro/video files, including IV32 content; [media_compatibility.md](reference-game/media_compatibility.md). |
| `.BMP`, `.ICO`, `.HLP`, `.WRI`, `.INF`, `.TXT` | **Hypothesis from names/types:** billboard bitmap, application icon, user guide, readme, language/setup metadata, version text. Archived demo `.BMP` is a separate format candidate and must not be assumed to be a Windows BMP. |
| `.BND` | **Confirmed:** 84 opaque definition-named resources / 4,840 bytes; several spans match compiled globals used by damage/UI code. **Hypothesis:** serialized module tuning defaults; original loader and complete grammar unresolved. [bnd.md](formats/bnd.md). |
| `.COL` | **Confirmed:** 22 same-stem vehicle/flyer collision definitions, nested model-part/DMG-region sphere lists. [collision.md](formats/collision.md). |
| `.DMG` | **Confirmed:** 22 damage definitions, capacity limits, regions and weighted links; actor part-damage consumer identified. [collision.md](formats/collision.md). |
| `.GL` | **Confirmed:** 21 vehicle attachment lists, 26-byte records linking DTS parts and damage/display slots. [collision.md](formats/collision.md). |
| `.PDG` | **Confirmed:** 22 vehicle damage-display copies including the SPIDER patch, plus a distinct WEAPONS screen-pair table; 23 complete parses. [collision.md](formats/collision.md). |
| `.DB0`, `.DB1`, `.DB2`, `.HB0`, `.HB1`, `.HB2`, `.ED0`, `.ED1`, `.ED2`, `.ED3`, `.HD0`, `.HD1`, `.HD2`, `.HD3`, `.GAU`, `.VUE` | **Hypothesis:** vehicle/cockpit resources and views. Each suffix has nine entries with matching vehicle names; resolution, image-versus-metadata and view-index meanings are unverified. |
| `.DCI`, `.EDG` | **Hypothesis:** cursor images and clipping/edge definitions, from `CURSOR.DCI`, `MFDCLIP.EDG`, `HDDCLIP.EDG`. |
| `.DGS`, `.FM`, `.NAM`, `.OFS` | **Hypothesis:** shape groups (`BASES`, `BHULKS`), flight-model data (`RAZOR`, `SKIMMER`), name lists (`MECHS`, `FLYERS`), and pilot offsets (`PILOT*`). Layouts and exact consumers unknown. |
| `.WLD`, `.RMP` | **Confirmed:** world/terrain resource layouts and selection paths are in [mission.md](formats/mission.md), with explicit remaining semantic limits. |
| `.SNC` | **Hypothesis:** voice/sound sequencing or synchronization data; 556 same-path resources occur in both `SIMSOUND` and `SIMVOL0`. Not proven to be audio samples. |
| Demo `.DIB`, `.PBA`, `.PFT`, `.PPL`, `.DT2`, `.DML`, `.TXL`; referenced `.TED` | **Hypothesis:** DIB images, PBA image arrays, PFT fonts, PPL palettes, DT2 models, DML material lists, TXL texture lists and TED terrain. These are A-10 data and are not substitutes for verified EarthSiege II schemas. **Confirmed:** no `.DML` member occurs in the 16 ES2 VOLN archives. |

The format documents record the independent reader and data verification.
COL/DMG/GL/PDG were derived directly from the original DBSIM and
all CD copies.

### Duplicate members

**Confirmed:** within `VOL/`, 567 case-folded paths repeat
across archives and 10 have different payloads. The original DBSIM sorts
archives by descending priority byte and returns the first match; see
[collision.md](formats/collision.md#selection-and-opening). SIMPATCH therefore
precedes SIMVOL0 when both are registered. Actual BND opening is unconfirmed.
Member totals count physical copies, not unique effective resources.

## Open formats

BND has no original reader, so its layout is not recovered
([bnd.md](formats/bnd.md)). SNC, OFS/DCI/EDG, DGS/FM and the cockpit metadata
types (DB0 to VUE) have no format document; the role table lists them as
hypotheses. All other types above are specified in the linked format documents
with their remaining exceptions.

## Reproducing the measurements

The scripts use only the Python standard library. From `<es2>`:

```powershell
& '<es2-dependencies>\python\python.exe' -B <es2-data>/logs/codex-vol-20261005/inventory.py
& '<es2-dependencies>\python\python.exe' -B <es2-data>/logs/codex-vol-20261005/payload_checks.py
```

Outputs: `<es2-data>/logs/codex-vol-20261005/inventory.json` (full listing, extension
counts, hashes, member metadata and bounds checks) and `payload-checks.json`
(WAV/header checks). No extracted members are written. The reports can be
regenerated; this document and [formats/vol.md](formats/vol.md) preserve the
findings and the archive hashes.

### Collision formats

Evidence is in `<es2-data>\logs\codex-formats-collision-20261007`:
`probe.py`, `audit.py`, `verification.json`, `rederive.py` and `rederive.json`.
All 24 archive-file hashes match the CD manifest. The complete 16-VOLN scan
accounts for 88 COL/DMG/GL/PDG payloads and 84 opaque BND payloads. The
scripts write no original members or decoded assets. Counts, exceptions,
static addresses and hypotheses are in [collision.md](formats/collision.md) and
[bnd.md](formats/bnd.md).
