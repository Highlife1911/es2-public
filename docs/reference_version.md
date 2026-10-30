# Reference Version

The EarthSiege II build that every address, hash and behaviour in this
repository refers to. See [README.md](../README.md#goal).

---

## 1. Source

| Field | Value |
|---|---|
| Item | `sierra-earth-siege-2` |
| URL | https://archive.org/details/sierra-earth-siege-2 |
| Title | EarthSiege 2 |
| Item date | 1996-03-10 |
| Published on archive.org | 2021-07-18 |
| Collections | cdromimages, vintagesoftware |
| License / rights field | not set |

Item description (summary): three ISO images of the 1996 game by Sierra / Dynamix:

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
| MD5 (archive.org) | `230dca9b700e37eea50a89359fe1b933` |
| SHA-1 (archive.org) | `42088ea2eeb35d031c8729d6d65b1e3137dfd4f2` |
| CRC32 (archive.org) | `86c3f828` |
| SHA-256 | `b192b7235833f7532fe2b8b4f100e02e9e87b984863c7c1eb0e9c186681f75ca` |
| Version | 1.11 (`VERSION.TXT` on the CD: "EarthSiege II version 1.11") |
| Language | German (GoldGames collection, according to description) |

### Image format

Despite the `.iso` extension, this file is **not** a standard ISO:

- 658 531 776 = 279 988 × **2352** exactly, so it is a **raw CD image (2352-byte sectors, BIN-style)**.
- There is **no CUE sheet** in the item. A CUE sheet must be created to mount the image with audio tracks.

Hypothesis (not verified):

- The data track has the same size as the data-only image: 207 193 sectors.
- The remaining 72 795 sectors (≈ 16 min 11 s at 75 sectors/s) are the audio tracks.
- Pregaps between tracks may shift these boundaries.

## 3. Cross-check images (same item)

| File | Size | Sector size | Role |
|---|---|---|---|
| `EarthSiege2_Freeware_GoldGames_1r11_dataOnly.iso` | 424 331 264 | 2048 (207 193 sectors) | Should match the data track of the reference. Used to verify extraction and track boundaries. |
| `Earthsiege2_Freeware_HiRezStudios_1r0.iso` | 352 212 992 | 2048 (171 979 sectors) | v1.0, no audio. Secondary reference: differences between 1.0 and 1.11 (executable, data). |

Hashes from archive.org:

| File | MD5 | SHA-1 |
|---|---|---|
| `…_GoldGames_1r11_dataOnly.iso` | `38bb570766c1e91fdb5f2a71db555216` | `ca3c2007f44014924c781672d3c1eeeee00d0d80` |
| `…_HiRezStudios_1r0.iso` | `ee0197049dc71323624aba2af6b76a75` | `4a157f0e0907ed756cd47ccc10ee0acb67b81264` |

## 4. Local copies and hashes

- The reference image lies unversioned in `<es2-dependencies>\archive\`; its MD5 matches archive.org.
- `tools/reference-game/setup/extract_data_track.py` writes the data track to `build\extracted\earthsiege2-data.iso` (SHA-256 `29d1e0ef…f5c72`); 7-Zip extracts its files to `build\extracted\cd` ([setup.md](reference-game/setup.md)).
- `data/iso_file_hashes.sha256`: every file on the data track (865 files) plus both images; used by the commit guard. Key binaries: `data/reference_hashes.txt`.
- Language: German, selected via `DATA/LANGUAGE.CFG` at install time (the CD contains English, French and German resources).

Open:

- The extracted track's MD5 (`299b9436…5b5bc`) differs from the published data-only MD5 (`38bb5707…55216`). The data-only image was not downloaded, so the cause (padding, mastering) is unknown.
- The audio track layout and a CUE sheet (CD music).
- Optional: the v1.0 image for comparison.

## 5. Consequences of this choice

- **German version:** all texts, briefings and possibly voice output are German. String handling and fonts must support German characters (code page to be determined).
- **Version 1.11:** a patched version. Behavior may differ from v1.0; the v1.0 image helps to find out where.
- **Audio included:** music can be tested from the start (format and integration open).
- **Raw image without CUE:** the start script mounts the data-only ISO that `extract_data_track.py` writes from it ([setup.md](reference-game/setup.md#prepare-the-original-media)).

## 6. Reference executables

The Win95 game uses three executables from `VER95/`; the installer copies them into the installation root.

| File | Size | SHA-256 | Role |
|---|---:|---|---|
| `VER95/ES.EXE` | 11 808 | `47593f5941922ad61d6903829afbe3515750989ec91aac13ce6733109f0f4384` | launcher |
| `VER95/VSHELL.EXE` | 566 304 | `1534aa8f732bdecd826484d893ad58b8a5e6ebd4d25ab3158a2469b8f69ba16e` | shell |
| `VER95/DBSIM.EXE` | 724 512 | `8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad` | simulation |

Runtime DLLs and further hashes: `data/reference_hashes.txt`. All addresses in `docs/` refer to these files at image base `0x00400000`.

All three are PE32 i386 GUI programs with image base `0x00400000`, entry
point `0x00401000`, linker version 2.25 and a relocation table; ASLR is not
requested and the PE timestamps decode to implausible years. They are built
with Borland C++ and import its runtime `CW3220.DLL`
([rtl_imports.md](reference-game/rtl_imports.md#the-runtime-version)). No
packer signature was found (no proof that there is no protection). None of the
three imports `CreateThread`. The other DLLs: `BWCC32.DLL` is loaded
dynamically, `VER95/SOS9503.DLL` imports WINMM, `SOSLIBS3.DLL` and the
installer `SETUP.EXE` are 16-bit NE files. Imports and IAT addresses per
executable: `es2-data\logs\pe-triage.json`;
[dll_imports.md](reference-game/dll_imports.md) and
[rtl_imports.md](reference-game/rtl_imports.md) for DBSIM.
