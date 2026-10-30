/* The residue table of own_process.md section 5, as data. Rows with a
 * width or offset of 0 are known readers whose exact bytes are still open. */
#include "es2/sim/residue.h"
#include <stddef.h>

static const es2_sim_residue rows[] = {
    {0x004231a4u, "group_order", ES2_RESIDUE_FRAME, -0x1c, 2, ES2_RESIDUE_ZERO, 0,
     "priority word [f-1Ch] for order codes 3, 6, 9..0Eh and above 0Fh",
     "zeroed scratch frame gives 0 (order given); the golden's word was >= -1 in c3_09-wingman-orders "
     "(Alt+O at frames 151, 1481); input.md \"The residue reader: the group order `0x4231a4`\""},
    {0x0041f5a0u, "aim_fire", ES2_RESIDUE_OBJECT, 0, 0, ES2_RESIDUE_ZERO, 0,
     "padding bytes of the +28h weapon bodies' matrices and points, the impact-exclusion dword "
     "(es2_weapon_graph.padding/composed_padding/excluded)",
     "open: the live difference of c3_09-loadout-b (frame 1671) was fixed by the shim, the consuming byte not "
     "identified (ai.md \"Stack residue under aim_fire\")"},
    {0x00478bc4u, "pose_parent", ES2_RESIDUE_OBJECT, 19, 1, ES2_RESIDUE_ZERO, 0,
     "byte 19 (padding) of the output matrix on some paths of the track functions 0x478bc4, 0x478f6c, "
     "0x478ff4, 0x4790b4, 0x479194 (track.h, the padding argument)",
     "copied, not used in arithmetic by the known consumers (weapons.md)"},
    {0x00478c64u, "node_matrices", ES2_RESIDUE_OBJECT, 19, 1, ES2_RESIDUE_WRITER, 0,
     "byte 19 of each child node matrix: pose_parent's [ebp-0Dh] is the slot where the root call 0x478c1c pushes "
     "its matrix pointer (0x478c54), so after a root node it is that pointer's high byte, before it the entry residue",
     "the original's after-load bytes +0x173 equal their block's high address byte (02/03, mission_load.md section "
     "14 \"Node matrix byte 19\"); the in-vitro residue probe of node_matrices is exact on a 31/a7 stack"},
    {0x00418e60u, "vehicle_value", ES2_RESIDUE_REGISTER, 0, 4, ES2_RESIDUE_ZERO, 0,
     "ECX left when no item was appended, under the results writer (scratch dword f-4)",
     "open (no reader named; results_writer.h)"},
    {0x0047a2c8u, "weapon_factory_class", ES2_RESIDUE_FRAME, -0x58, 0, ES2_RESIDUE_ZERO, 0,
     "projectile record local [f-58h] when the selector is 21h (the load's weapon factory)",
     "open (mission_load.h)"},
    /* input.md, "the input layer as own code", stack residue: readers in the own input layer. */
    {0x0045b758u, "mouse_bounds", ES2_RESIDUE_FRAME, 0, 6, ES2_RESIDUE_ZERO, 0,
     "three words at ESP+0x10..+0x15 of its 0x18-byte local (no EBP frame), copied by 0x480764 to mouse+0x1e..+0x23",
     "open: no reader of mouse+0x1e..+0x23 named; in vitro the original wrote stack bytes there (masked)"},
    {0x0045baa8u, "device_poll", ES2_RESIDUE_FRAME, 0, 0x1d, ES2_RESIDUE_ZERO, 0,
     "the 0x1d-byte joystick local at ESP+0xc (no EBP frame) copied to D+0xd.. when the reader 0x45bc10 did not write it "
     "(a registered joystick with an inactive record or a failed poll)",
     "open: needs a registered joystick (the two joystick suites); control_record.h names the same read"},
    {0x0045c330u, "joystick_poll", ES2_RESIDUE_FRAME, -0x30, 0x12, ES2_RESIDUE_ZERO, 0,
     "the axes and button bytes of its local [ebp-30h] when the response mode is above 2 or the device's bit 0 is clear",
     "open: the constructor sets mode 1 and the poll runs only for an active device with bit 0 set"},
    /* terrain.md, "the terrain renderer as own code", stack residue: the terrain cell draw. */
    {0x0046d448u, "terrain_cell_draw", ES2_RESIDUE_FRAME, -0x54, 0x10, ES2_RESIDUE_ZERO, 0,
     "the shade words [ebp-4Ch] (first polygon) and [ebp-54h] (second triangle), four words each copied zero-extended to "
     "0x6b4f6c..0x6b4f7b for the textured fills; only the first word of each is written, and only in fill mode 0 with "
     "shade mode below 2 or fill mode 1 with shade mode 1",
     "open: no reader of the unwritten words named; in vitro the four dwords 0x6b4f6c are masked and the written word "
     "is compared through the fill recorder"},
    /* own_process.md section 5: readers checked against the
     * original's writers; the zero model holds for each. */
    {0x0046254cu, "sound_table_mode", ES2_RESIDUE_FRAME, 0, 0x14, ES2_RESIDUE_ZERO, 0,
     "its 20-byte local at ESP+0 (no EBP frame; EBP is 0x4d2d68), never written, searched by _strstr for \".hmp\" "
     "(open mode 0) and \".wav\" (mode 1); found neither, the mode is the entry EDI, 0x462410's channel array",
     "neutral: in the original the local holds bytes 12..27 of 0x46379c's profile buffer [ebp-28h] (not reached by the "
     "short sound.cfg values) and the EBP that 0x463694 saved, both left by 0x462410's earlier callees; mode 1 and the "
     "pointer open alike (digital), only \".hmp\" would change the open, and DBSIM's one \".hmp\" string is the constant "
     "it searches for; audio.md \"Facts read on the way\""},
    {0x00465e28u, "palette_local_table", ES2_RESIDUE_FRAME, 0, 0x400, ES2_RESIDUE_ZERO, 0,
     "the 1 KiB colour table local: an entry whose colour has neither flag bit is not written, and byte 3 "
     "(rgbReserved) of every entry never is",
     "neutral: every colour DBSIM installs has flag bit 0 (the DPL records all 01, dpl.md; the shield and intro entries "
     "0x01000000 | rgb; the cycler and the damage colours copy palette entries), so only byte 3 is residue; 0x465a24 "
     "takes bytes 0..2 and sets its own flags, SetDIBColorTable's rgbReserved is not displayed, DBSIM imports no "
     "GetDIBColorTable, and both frame hashes use bytes 0..2 (presentation.md \"Deviations\")"},
    {0x0042e8e8u, "shade_ranges_flag_path", ES2_RESIDUE_FRAME, -0x10, 4, ES2_RESIDUE_WRITER, 0,
     "the flag path's second pair for 0x430590: the list's first dword, the stack address of p1 (low word the start, "
     "high word the length); an original bug, the words written depend on the stack address",
     "unreachable: the own frame is the original's (EBP = the arguments - 8), so equal call-area addresses write "
     "equal words; both callers pass a flag with a clear low byte: 0x46bd6e pushes 0, 0x4287b3 the byte 0x4cf984, "
     "which 0x428d0a sets from byte 0x4d24c5 (view block B+9), cleared by 0x45caf4's memset and written by no code "
     "(listing scan); rasterizer.md \"View-space projection and shade ranges\""},
    {0x00451de0u, "hdd_parts_ecx", ES2_RESIDUE_REGISTER, 0, 4, ES2_RESIDUE_ZERO, 0,
     "ECX = its second local (pop ecx; pop ecx) when no entry was drawn",
     "dead: the HDD draw that calls it is own code and 0x42f0b0 does not use ECX after the call (own_process.md "
     "\"The popped locals\")"},
    {0x0045c964u, "blit_at_z_ecx", ES2_RESIDUE_REGISTER, 0, 4, ES2_RESIDUE_ZERO, 0,
     "ECX = its second local (pop ecx; pop ecx) without a bitmap",
     "dead at its callers, as hdd_parts (own_process.md \"The popped locals\")"},
    {0x0048cce4u, "projection_edx", ES2_RESIDUE_REGISTER, 0, 4, ES2_RESIDUE_ZERO, 0,
     "EDX without a focal shift: its never-written local",
     "dead: the only caller 0x46bcb0 (the flat fill 0x46bc44) writes EDX before any read on both result paths "
     "(0x48cf20's loop, 0x46bcdc..0x46bd2d; listing 2026-10-23)"},
};

unsigned es2_sim_residue_count(void) { return (unsigned)(sizeof(rows) / sizeof(rows[0])); }
const es2_sim_residue *es2_sim_residue_row(unsigned i) { return i < es2_sim_residue_count() ? &rows[i] : NULL; }
const es2_sim_residue *es2_sim_residue_find(uint32_t reader) {
    for (unsigned i = 0; i < es2_sim_residue_count(); ++i) if (rows[i].reader == reader) return &rows[i];
    return NULL;
}
