# Reference Version

The EarthSiege II build that every address and behaviour in this repository refers to.
See [README.md](../README.md).

## 1. Source

The archive.org item `sierra-earth-siege-2` (https://archive.org/details/sierra-earth-siege-2)
holds three images of the 1996 game by Sierra / Dynamix:

- the Hi-Rez Studios freeware release (v1.0, no audio),
- a German GoldGames collection copy (v1.11, with audio),
- a data-only version of the GoldGames copy.

## 2. Reference image

```text
EarthSiege2_Freeware_GoldGames_1r11_withAudio.iso
```

| Field | Value |
|---|---|
| Size | 658 531 776 bytes |
| Version | 1.11 (`VERSION.TXT` on the CD: "EarthSiege II version 1.11") |
| Language | German (GoldGames collection); `DATA/LANGUAGE.CFG` selects it at install time, the CD contains English, French and German resources |

Despite the `.iso` extension the file is a **raw CD image** with 2352-byte sectors
(658 531 776 = 279 988 × 2352) and no CUE sheet. Its data track has the size of the
data-only image (207 193 sectors); the remaining sectors are the audio tracks.

The data-only image of the same item (2048-byte sectors) matches the data track; the v1.0
image is a secondary reference for differences between 1.0 and 1.11.

## 3. Use

The image is a dependency in `es2-dependencies` ([dependencies.md](dependencies.md#cd-image)).
`tools/reference-game/setup/extract_data_track.py` writes the data track to
`build/extracted/earthsiege2-data.iso` and 7-Zip extracts its files to `build/extracted/cd`
([setup.md](reference-game/setup.md)). `data/iso_file_hashes.sha256` lists every file of the
data track and both images (used by the commit guard); `data/reference_hashes.txt` the key
binaries.

Open: the audio track layout as a CUE sheet.

## 4. Reference executables

The Win95 game uses three executables from `VER95/`; the installer copies them into the
installation root.

| File | Size | Role |
|---|---:|---|
| `VER95/ES.EXE` | 11 808 | launcher |
| `VER95/VSHELL.EXE` | 566 304 | shell |
| `VER95/DBSIM.EXE` | 724 512 | simulation |

All addresses in `docs/` refer to these files at image base `0x00400000`.

All three are PE32 i386 GUI programs with image base `0x00400000`, entry point `0x00401000`,
linker version 2.25 and a relocation table; ASLR is not requested and the PE timestamps
decode to implausible years. They are built with Borland C++ and import its runtime
`CW3220.DLL` ([rtl_imports.md](reference-game/rtl_imports.md#the-runtime-version)). No packer
signature was found (no proof that there is no protection). None of the three imports
`CreateThread`. `BWCC32.DLL` is loaded dynamically, `VER95/SOS9503.DLL` imports WINMM,
`SOSLIBS3.DLL` and the installer `SETUP.EXE` are 16-bit NE files. Imports of DBSIM:
[dll_imports.md](reference-game/dll_imports.md), [rtl_imports.md](reference-game/rtl_imports.md).
