# EarthSiege II DPL palettes

Status: all 65 DPL members of the reference CD.
Field meanings below come from bytes and static original-reader analysis
unless marked **hypothesis** or **unknown**.
All integers are little-endian; offsets are relative to the VOL member payload.
See [vol.md](vol.md) for the outer container and [dbm.md](dbm.md) for images.

## Evidence

An independent standard-library verifier inspected all 3,802 entries of the
16 VOLN archives: 13 under `extracted/cd/VOL`, plus three voice archives under
`VER31`. Every archive and every inspected image/palette/font member matched
[iso_file_hashes.sha256](../../data/iso_file_hashes.sha256).
The archive hash list is described in [vol.md](vol.md#cd-archive-hashes).

**Binary identities:** `VER95/VSHELL.EXE` and `VER95/DBSIM.EXE`
(hashes: [reference_hashes.txt](../../data/reference_hashes.txt)).

Addresses use image base `0x00400000`. VSHELL returns the DPL tag at
`0x4542b0` and reads its body at `0x454334`; DBSIM's matching body reader is
`0x4821cc`. These are static observations, not runtime oracle tests.

## Serialized layout

Let `C` be the colour count and `T = 12 + 4*C`.

| Offset | Width | Meaning / observed values |
| --- | --- | --- |
| `0x00` | 4 | Tag `0f 00 28 00`. |
| `0x04` | 4 | Body byte length `S`; every member has exactly `8 + S` bytes. |
| `0x08` | 2 | `C`, colour count; always 256. |
| `0x0a` | 2 | Always zero; kept at object `+6` and passed through `0x454223` to `0x457d88` as its fourth argument, which loads it and does not use it. |
| `0x0c` | `4*C` | Colour records: red, green, blue, flags, one byte each. |
| `T` | 2 | Number `R` of colour ramps; either 0 or 256. |
| `T + 2` | 2 | First ramp slot in the renderer's ramp table; always zero here. |
| `T + 4` | variable | `R` consecutive ramp records, described below. |

The colour count is **not a proven 32-bit count**: VSHELL reads two separate
16-bit fields at `0x454343–0x45435a`, then reads `4*C` bytes at
`0x45437e–0x454390`. The zero at `0x0a` makes a 32-bit interpretation appear
to work on this corpus. Its meaning remains unknown; do not combine the fields.
The same two-read sequence occurs at DBSIM `0x4821db–0x4821f6`.

**Colour representation:** all 16,640 colour records have RGB
components in `0..63` and flags `01`, including palette index zero.
VSHELL `0x407242` tests flag bit 0, and `0x407248–0x407267` shifts each
component left by two into a Windows BGR buffer (blue, green, red per
entry). That buffer goes **unchanged** to `SetDIBColorTable` (`0x4072f3`):
the frame's colours are the DPL components × 4 (`63 -> 252`), not
normalized to 255. Its fourth byte is never written, and an entry whose
flags have neither bit 0 nor bit 1 is not written at all (uninitialised
stack in both cases). Flag bit 1 selects an unscaled byte-component
path at `0x407270–0x407294`; no CD DPL exercises that path.

The logical (screen) palette is built separately by `0x406f40`:
`0x406f80–0x406fa0` restores RGB order only in its `SYSPAL_NOSTATIC`
branch; the usual branch (`0x407042..0x407121`) does the same for entries
r..255−r only, keeps the r = `NUMCOLORS`/2 static system colours at both
ends and moves the blue component of an entry that equals a static colour
by ±3. This does not affect the frame's colour table
([vshell_presentation.md](../reverse-engineering/vshell_presentation.md)).

The fourth byte is **flags, not alpha**. These palette records do not encode
transparent index zero. Bitmap zero-index transparency is controlled by the
bitmap drawing path; see [dbm.md](dbm.md#rows-and-transparency).
Other flag combinations are open.

## Colour ramps; shade, haze and translucency

**Representation:** each ramp begins with a 16-bit length `L`,
followed by `L` 16-bit palette indices. There is no offset table or padding
between ramps. VSHELL `0x4543d2–0x454408` reads exactly this sequence.
Every index in the corpus is within `0..255`; lengths are 1–25.
The 37 palettes with ramps each have 256 records (9,472 ramps in total).
The other 28 end immediately after the zero ramp-count/base pair.

Every DPL satisfies:

`S = 8 + 4*C + sum(2 + 2*L for each ramp)`.

**Renderer use:** `0x45414c–0x45416c` passes the ramp base, count
and records to `0x457d88`, which installs them in the table at `0x47ddd5`
using eight-byte in-memory descriptors. At `0x455bd6–0x455c08`, colour-kind
4 selects a ramp with the low 16 bits of a colour value and selects a ramp
element using its next byte, effectively `floor(level * L / 256)`.
`0x45879b–0x4587c9` also selects ramp entries using a scaled level.
This confirms shade/gradient lookup use; ramp number is not inherently
the source bitmap's palette index. A specific lighting or atmospheric
interpretation of every ramp is **unverified**.

**Examples:** `SHELL0:DPL\PALETTE.DPL` ramp 2 has length 16 and
indices 112–127; its ramp 19 is the single index 255. `DPL\MAP.DPL` ramp 0
contains indices 104–109. Ordered ramps can also descend: MAP ramp 16 is
199, 198, 197. Do not require increasing indices or a fixed ramp length.

**Scope limit:** all bytes in all 65 files are accounted for by
the header, RGB/flags records and these ramps. No additional serialized
haze table, alpha plane or source/destination translucency matrix occurs
in these DPL members. This does not rule out tables stored elsewhere or
constructed at runtime. Calling the zero fields “haze levels” is unsupported.

## Corpus results and examples

**Full-consumption results**, without deduplicating repeated content:

| Archive | DPL members | With 256 ramps | Without ramps |
| --- | ---: | ---: | ---: |
| `VOL/SHELL0.VOL` | 33 | 17 | 16 |
| `VOL/SIMVOL0.VOL` | 32 | 20 | 12 |
| Other 14 VOLN archives | 0 | 0 | 0 |
| **Total** | **65** | **37** | **28** |

**Sample locations**, using absolute VOL payload offsets:

| Member | Payload offset | Bytes |
| --- | --- | ---: |
| `SHELL0:DPL\MAP.DPL` | `0x853907` | 2,398 |
| `SHELL0:DPL\MAP_PAL1.DPL` | `0x85426f` | 1,040 |
| `SHELL0:DPL\PALETTE.DPL` | `0x854aa3` | 2,306 |
| `SIMVOL0:dpl\WORLD0.DPL` | `0x81f02a` | 2,284 |

MAP_PAL1 starts `0f 00 28 00 08 04 00 00 00 01 00 00` and ends with
the four zero bytes at `0x40c`. For PALETTE, `T = 0x40c` contains
`00 01 00 00`: 256 ramps starting at slot zero, not 256 extra colours.

## Evidence and open points

The layout, the tail schema and the colour scale come from the original
readers and the CD bytes.

Palette-to-image bindings remain **hypotheses**, listed in [dbm.md](dbm.md#palette-selection).
Open: nonzero `0x0a`, unused colour flags, nonzero ramp bases, runtime lighting
semantics and exact final displayed colours after platform palette handling.
