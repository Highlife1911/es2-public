# Herc picture tables (`gam\arm_*.dat`, `gam\rpr_*.dat`, `gam\*_hots.dat`)

Status: layout (serialized form: [dat.md](dat.md), section "Shell
graphics") and roles from the consumers. Static
reading of the reference `VER95\VSHELL.EXE` (llvm-objdump listing, image base `0x00400000`); the files of the CD parse with
the own reader (`es2/shell/gui/squad_pictures.h`) in the real case of
`//shell:screen_test --suite squad_pictures`, and a scratch walk of the 20
members consumed every byte. The drawing that uses the
tables is [vshell_ui.md](../reverse-engineering/vshell_ui.md) section 36.

All integers are little-endian. File names: `gam\arm_<herc>.dat` and
`gam\rpr_<herc>.dat` with `<herc>` = `outl`, `rapt`, `toma`, `sams`, `colo`,
`apoc`, `ogre`, `mavr`, `razr` for Herc types 0..8 (tables `0x47006c`,
`0x46ffd4`); `gam\arm_hots.dat`, `gam\rpr_hots.dat` (literals `0x47661d`,
`0x47662e`). All are VOL members (`SHELL0`/`SHELL1`).

## Records

The readers copy each file record into a 26-byte (`0x1a`) memory record with
the stream's read method (`vtable +0x18`, two bytes) and `0x403985` (a plain
`_read` of n bytes, `0x46ba00`):

| Memory | File (A22 / R14) | Role (consumer) |
|---|---|---|
| `+0x00` word | `+0` / `+0` | the ESGrid layer index the picture goes to (`0x40be7f`'s index); in a slot list also the **key**: slot + 2 (arm) or slot + 6 (rpr), matched by `0x4142f0` |
| `+0x02`, `+0x06` dwords | `+2`, `+6` | the layer's draw point (x, y) in the panel |
| `+0x0a`, `+0x0e` dwords | `+10`, `+14` (A22 only) | the slot marker's draw point (`0x415bff`) |
| `+0x12` word | `+18` / `+10` | DBA frame index (signed); `-1` in an arm slot list: no picture |
| `+0x16` dword | `+20` / `+12` (a word) | the layer's mirror word `+0x34` (values 0, 1, 2 on the CD); the high half is never written (zero in the image) |

In the slot lists of the arm files the loader adds 1 to all four coordinates
after reading them (`inc dword` at `0x415335`, `0x415353`, `0x41539f`,
`0x4153bd`); the two leading arm records and every rpr record are kept as read.

## `gam\arm_<herc>.dat` (reader `0x41508f`, at start-up)

| Part | Into | Use |
|---|---|---|
| A22 | `0x484444 + 0x1a t` | the Herc's body layer (`0x41547f`) |
| A22 | `0x48452e + 0x1a t` | its outline layer |
| word n, then n × (word item ID, word count, count × A22) | count `0x484702 + 0xc6 t + 2 id`, list pointer `0x484744 + 0xc6 t + 4 id` | per item ID (0..32, unchecked: 33 counts then 33 pointers per type) the pictures by slot key; ID 0's list gives the empty slot's marker point |

A later group of the same item ID replaces the count and pointer of an earlier
one (the own reader does the same; none occurs on the CD).

## `gam\rpr_<herc>.dat` (reader `0x414340`, at start-up)

| Part | Into | Use |
|---|---|---|
| word b, b × R14 | count `0x484df8 + 2t`, list `0x484e0c + 4t` | REPAIR's part layers (`0x4146cd`) and BUILD's type pictures (`0x415dc1`); the layer word is also the part group of the diagram's colour (`0x414cbe`: the word, minus 16 when 16 or more) |
| R14 | `0x484618 + 0x1a t` | REPAIR's internal picture (`*_int.dba`) |
| word n, then n × (word item ID, word count, count × R14) | `0x484e30 + 0xc6 t` (counts), `0x484e72 + 0xc6 t` (pointers) | REPAIR's weapon pictures (`rpr_wpns.dba`) by slot key slot + 6 |

## `gam\arm_hots.dat`, `gam\rpr_hots.dat` (reader `0x43c830`, at every load)

Word 9 (asserted, `0x43c9ae`), then per type: a word (read into a temporary,
not used), a word count (`0x48d65c + 2i`) and count rectangles of four dwords
(left, top, right, bottom in the panel: the ESRect constructor `0x40aca4`
takes them as its rectangle) into `0x4763c4[i]`; entry i belongs to type i.
VSHELL builds an ESRect per rectangle (WEAPONS: one per slot below the
capacity; REPAIR: the type's rectangles with the External rows' handlers, then
one per mounted slot from `0x414a3c`), but never shows them (vshell_ui.md
section 36.3).

## Verification

- Serialized layout and exact consumption: dat.md and a scratch
  walk of the CD members (every arm/rpr item and slot key pair
  checked: each arm (item, slot) with a picture has an rpr record for the same
  item and slot).
- Roles: the listing ranges named in each table.
- Own reader: `//shell:squad_pictures_test` (synthetic files, every
  truncation refused, IDs outside 0..32 and negative counts refused, later
  groups replacing earlier ones) and the real case of the screen suite.
