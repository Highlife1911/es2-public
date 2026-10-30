r"""Data inputs that reach one of the original's divide sites (own_process.md "The original's fault paths").

Each scenario changes files the mission load reads, in a game directory:

  zone-zero    dat\zone<ID>.dat, the zone DAT with its fourth dword 0: the terrain slopes' idiv 0x46c208
               divides by it at mission load. DBSIM's opener 0x4730e8 opens the loose file before the VOL
               (vol.md "Lookup and loose files"); <ID> is the script.dat header word 1.
  zone-flat    the same file with the fourth dword 16385: the slopes' quotient (1 << third) / 16385 is 0, so
               a flat cell gives a zero normal and normalize's idiv 0x46c29e divides by its length 0.
  zone-null-cell  the same file with the third dword 31 and the fourth -1: the base-mark raster 0x47136c
               (actor load) shifts the building's box out of the grid, gets the cell pointer 0 and reads
               [0 + 0xf] at 0x471455, an access violation on the NULL page before the slopes' idiv runs.
  zone-overflow  zone-null-cell's DAT and dat\mat0.dat with every material's level (+4) 22: the raster's
               shift counts are taken mod 32, so its coordinate shift is 31 and its cell shift 0, every box
               cell index is 0 (inside the grid), and the slopes' (1 << 31) / -1 overflows at 0x46c208 (#DE,
               reported as 0xc0000095).
  ladder       DATA\script.dat with the zone (header word 1) of the first ZONES.VOL heightmap of the same size
               whose highest pixel p divides 5000 (zone 508, p = 100), and that zone's dat\zone<ID>.dat with the
               fourth dword (the height scale) -5000 / p: the terrain's floor is 0 and its ceiling p times the
               scale (0x46c4c4), so the altitude ladder's range ceiling + 5000 - floor is 0 and its idiv
               0x43decf faults; the ladder draws for a flyer only (c4_08-slope-dive). remove leaves script.dat.
  map-columns  DATA\script.dat with two more points whose x values span 2^31 - 1 and -2147358112: the map
               set-up's 32-bit column count is 0 for a map shift (the zone DAT's third dword)
               of 13 to 16, and the idiv 0x44f905 faults.
  map-rows     the same with the y values: the row count is 0, idiv 0x44f8f3.
  map-scale    DATA\script.dat with every point's x set to the first point's: the map page's x scale
               (half the x range idiv the page's half width, 0x44c461) is 0, so is the map scale M+0x20, and
               the projection 0x48c5fc divides by a zero depth at 0x48c63b at mission load.
  frustum-far  the zone DAT with the third dword 18 and dat\mat0.dat with every level 3 (keeps the base-mark
               raster in the grid for that shift): the terrain's far distance G+0x10c, the detail table word
               (6, 10 or 14) sar (third - 14) (0x46cbd4..0x46cbf2), is 0, the frustum 0x4949e4 gets the depth 0
               and the first plane normal (0, 0, 0), and 0x47e450's divide by its length faults at 0x47de3c.

The panel layouts gau\<herc>.GAU (0x6a4 bytes, read by 0x431778; loose files win as above), written for all
nine hercs of VOL\SIMVOL0.VOL with one dword changed:

  map-page-columns  L+0x4dc = L+0x4cc + 8: the map page's half width ((L+0x4dc - L+0x4cc) << sx - 8 << sx)
               sar 1 (0x44c28c) is 0, idiv 0x44c461 at the HDD construction.
  map-page-rows     L+0x4d8 = L+0x4d0 + 4: the half height is 0, idiv 0x44c47f.
  throttle     L+0x404 = L+0x3fc + 6: the throttle slider's travel (rectangle height less the knob
               bitmap's 6 << sy) is 0, so its step (span << 16) / range is 0 and the slider value's idiv
               0x452660 (vertical) divides by it on a left click on the throttle (the panel's mouse handler
               0x452898 calls slot +0x10); no c1_01 suite clicks it.
  heading-tape L+0x458 = L+0x450: the heading tape's width D+0x28 (0x43b604) is 0 and the range set
               0x43b7dc, called right after the tape's construction (0x43ca6b), faults at 0x43b7f3.

The vehicle configurations dat\<herc>.DAT (0xd8 bytes, dat.md), written for the nine hercs with a panel layout
with one word 0:

  shield-zero  +0xbe (the shield maximum, copied to P+0x228/+0x22a at creation, 0x413a90): the shield balance
               0x413bc8 divides by P+0x22a at 0x413c2c for the player at mission load.
  speed-scale-zero  +0xc2 (the ground rescale denominator): the mech type load divides +0xc4 * 400 by it
               through 0x47e068 at 0x4202f2 when +0x4e is 0; faults at 0x47e078.
  forward-speed-zero  +0x04 (the forward speed): cockpit_sync 0x41b130 divides speed << 10 by it at 0x41b26b
               at mission start.

The flight models fm\<name>.FM (0x36 bytes; RAZOR and SKIMMER), read into the configuration at +0x1dc:

  flight-model-zero  FM+0x2e = FM+0x2a: the type loads divide (FM+0x22 - FM+0x26) << 16 by FM+0x2a - FM+0x2e
               through 0x47e068 (0x42044b mech, 0x422fa1 flyer); faults at 0x47e078 at mission load.
  flight-model-overflow  FM+0x2e = FM+0x2a - 1: the same divide by 1 overflows (54000 << 16, RAZOR); the
               original faults with 0xc0000095 at 0x47e078. es2sim continues: the core's type load
               (core/src/type_loads.c) wraps the quotient, a known deviation.
  hud-speed-zero  FM+0x2a = 0 and FM+0x2e = FM+0x2e - FM+0x2a (the type loads' divisor unchanged): the HUD
               speed 0x41bb3c of a herc with +0x4e set (RAZOR) calls scalar_interpolate 0x47df48 with
               x1 = FM+0x2a = 0, which faults at 0x47df7a when the panel draws (c4_08-razor-plasma).

  cell-anim    VOL\SIMVOL0.VOL with every TSCellAnimPart's child count 0 (the slot word moved behind it, the
               body length kept; the copy is restored by remove): the cell animation 0x4768f0 divides the
               cell word by the count at 0x47691c when the shape draws.

  divide_scenarios.py write <scenario> <dir>     writes the file into <dir> (a clone or a work directory)
  divide_scenarios.py remove <scenario> <dir>    removes the loose files (restores the VOL for cell-anim)
  divide_scenarios.py replay <scenario> <replay_suite.py arguments>
                                                 a replay whose work directory carries the scenario
"""
import os
import struct
import sys

ZONE_FIELDS = {"zone-zero": (None, 0), "zone-flat": (None, 16385), "zone-null-cell": (31, -1),
               "zone-overflow": (31, -1), "frustum-far": (18, None)}
OVERFLOW_LEVEL = 22
# mat0.dat levels that keep the base-mark raster's box cells inside the grid for the scenario's third dword
LEVELS = {"zone-overflow": OVERFLOW_LEVEL, "frustum-far": 3}
MAP_AXIS = {"map-columns": 0, "map-rows": 1}
# panel layout dword at the first offset = the dword at the second plus the third
GAU_FIELDS = {"map-page-columns": (0x4dc, 0x4cc, 8), "map-page-rows": (0x4d8, 0x4d0, 4),
              "throttle": (0x404, 0x3fc, 6), "heading-tape": (0x458, 0x450, 0)}
GAU_SIZE = 0x6a4
# flight-model dword +0x2e = the dword +0x2a plus this
# flight model: FM+0x2e = FM+0x2a + the value; None: FM+0x2a 0 with the difference FM+0x2a - FM+0x2e kept
FM_FIELDS = {"flight-model-zero": 0, "flight-model-overflow": -1, "hud-speed-zero": None}
FM_SIZE = 0x36
# vehicle configuration word set to 0
VEHICLE_FIELDS = {"shield-zero": 0xbe, "speed-scale-zero": 0xc2, "forward-speed-zero": 0x04}
VEHICLE_SIZE = 0xd8
SCENARIOS = (tuple(ZONE_FIELDS) + ("ladder",) + tuple(MAP_AXIS) + ("map-scale",) + tuple(GAU_FIELDS) +
             tuple(FM_FIELDS) + tuple(VEHICLE_FIELDS) + ("cell-anim",))
# scenarios that write members of VOL\SIMVOL0.VOL as loose files: the subdirectory, the extension, the patch
LOOSE = {**{k: ("gau", ".GAU", lambda data, k=k: gau(data, k)) for k in GAU_FIELDS},
         **{k: ("fm", ".FM", lambda data, k=k: flight_model(data, k)) for k in FM_FIELDS},
         **{k: ("dat", ".DAT", lambda data, k=k: vehicle(data, k)) for k in VEHICLE_FIELDS}}


def vol_names(data):
    """The member names of a VOLN archive (vol.md), without directory, with each row's offset."""
    def need(at, n):
        if at < 0 or at + n > len(data):
            raise ValueError("VOL truncated at %d" % at)
    need(0, 12)
    if data[:4] != b"VOLN":
        raise ValueError("not a VOLN archive")
    dir_bytes = struct.unpack_from("<H", data, 10)[0]
    need(12, dir_bytes + 2)
    count = struct.unpack_from("<H", data, 12 + dir_bytes)[0]
    table = 18 + dir_bytes
    for i in range(count):
        row = table + 18 * i
        need(row, 18)
        yield data[row:row + 13].split(b"\0")[0].decode("ascii", "replace"), row


def vol_member(data, wanted):
    """The payload of member `wanted` (case-insensitive, without directory) of a VOLN archive (vol.md)."""
    for name, row in vol_names(data):
        if name.upper() != wanted.upper():
            continue
        body = struct.unpack_from("<I", data, row + 14)[0]
        if body + 9 > len(data):
            raise ValueError("VOL truncated at %d" % body)
        size = struct.unpack_from("<I", data, body + 1)[0]
        if body + 9 + size > len(data):
            raise ValueError("VOL truncated at %d" % (body + 9))
        return data[body + 9:body + 9 + size]
    raise KeyError(wanted)


DTS_PART_LISTS = (0x00140007, 0x00140008, 0x0014000b, 0x0014000c, 0x00140015, 0x001e0003)
DTS_CELL_ANIM = 0x0014000b
CELL_ANIM_BACKUP = ".cell-anim"


def dts_cell_parts(data):
    """The offsets of the child-count words of every TSCellAnimPart in a DTS resource (dts.md: objects of a tag,
    a body length and the body, padded to even; the part-list classes start with 12 bytes, a word count and the
    children)."""
    found = []

    def need(at, n, end):
        if at < 0 or at + n > end:
            raise ValueError("DTS object past its parent at %d" % at)

    def walk(at, end, depth):
        if depth > 64:
            raise ValueError("DTS nested too deep")
        while at < end:
            need(at, 8, end)
            tag, length = struct.unpack_from("<II", data, at)
            body, stop = at + 8, (at + 8 + length + 1) & ~1
            need(body, length, end)
            if tag in DTS_PART_LISTS:
                need(body, 14, body + length)
                count = struct.unpack_from("<H", data, body + 12)[0]
                if tag == DTS_CELL_ANIM:
                    found.append(body + 12)
                child = body + 14
                for _ in range(count):
                    need(child, 8, body + length)
                    child_end = (child + 8 + struct.unpack_from("<I", data, child + 4)[0] + 1) & ~1
                    walk(child, min(child_end, body + length), depth + 1)
                    child = child_end
            at = min(stop, end)

    walk(0, len(data), 0)
    return found


def cell_anim_dts(data):
    """The DTS with every TSCellAnimPart's child count 0 and its slot word moved behind the count; the body
    length stays, and the loader drains the rest of the body (0x47a83c)."""
    out = bytearray(data)
    for at in dts_cell_parts(data):
        count = struct.unpack_from("<H", data, at)[0]
        child = at + 2
        for _ in range(count):
            child = (child + 8 + struct.unpack_from("<I", data, child + 4)[0] + 1) & ~1
        if child + 2 > len(data):
            raise ValueError("TSCellAnimPart slot word past the file")
        struct.pack_into("<HH", out, at, 0, struct.unpack_from("<H", data, child)[0])
    return bytes(out)


def cell_anim_vol(vol):
    """The VOLN archive with every DTS member patched by cell_anim_dts in place (same size, so no row moves),
    and the number of members changed."""
    out, changed = bytearray(vol), 0
    for name, row in vol_names(vol):
        if not name.upper().endswith(".DTS"):
            continue
        member = vol_member(vol, name)
        patched = cell_anim_dts(member)
        if patched != member:
            body = struct.unpack_from("<I", vol, row + 14)[0] + 9
            out[body:body + len(member)] = patched
            changed += 1
    return bytes(out), changed


def zone_id(script):
    if len(script) < 4:
        raise ValueError("script.dat shorter than its header")
    return struct.unpack_from("<h", script, 2)[0]


def zone_dat(original, field4, field3=None):
    if len(original) != 16:
        raise ValueError("a zone DAT is 16 bytes")
    a, b, c, d = struct.unpack("<4i", original)
    return struct.pack("<4i", a, b, c if field3 is None else field3, d if field4 is None else field4)


def map_script(script, axis, other=2300000):
    """script.dat with two points appended (20-byte header, word count at 20, 12-byte points at 22)."""
    if len(script) < 22:
        raise ValueError("script.dat shorter than its header")
    count = struct.unpack_from("<h", script, 20)[0]
    at = 22 + 12 * count
    if count < 0 or at > len(script):
        raise ValueError("script.dat point count past the file")
    points = []
    for v in (-2147358112, 0x7fffffff):
        p = [other, other, 0]
        p[axis] = v
        points.append(struct.pack("<3i", *p))
    out = bytearray(script)
    out[at:at] = b"".join(points)
    struct.pack_into("<h", out, 20, count + 2)
    return bytes(out)


def map_scale_script(script):
    """script.dat with every point's x set to the first point's (x range 0)."""
    if len(script) < 22:
        raise ValueError("script.dat shorter than its header")
    count = struct.unpack_from("<h", script, 20)[0]
    if count < 1 or 22 + 12 * count > len(script):
        raise ValueError("script.dat point count past the file or zero")
    out = bytearray(script)
    x = struct.unpack_from("<i", script, 22)[0]
    for i in range(count):
        struct.pack_into("<i", out, 22 + 12 * i, x)
    return bytes(out)


def dba_top(dba):
    """The highest pixel of a zone heightmap: a DBA of one bitmap, the words h and w at 20 and 22, the h * w
    pixels last."""
    if len(dba) < 24:
        raise ValueError("DBA shorter than its header")
    h, w = struct.unpack_from("<hh", dba, 20)
    if h <= 0 or w <= 0 or h * w > len(dba) - 24:
        raise ValueError("DBA pixels past the file")
    return max(dba[len(dba) - h * w:])


def ladder_zone(zones, size):
    """The first zone of ZONES.VOL whose heightmap has `size` bytes and a highest pixel that divides 5000."""
    for name, _ in vol_names(zones):
        if name.upper().startswith("ZONE") and name.upper().endswith(".DBA"):
            dba = vol_member(zones, name)
            top = dba_top(dba)
            if len(dba) == size and top and 5000 % top == 0:
                return int(name[4:-4]), top
    raise KeyError("no zone heightmap whose highest pixel divides 5000")


def mat0(original, level=OVERFLOW_LEVEL):
    """The material table mat0.dat (a dword count, then 8-byte records) with every record's level +4 set."""
    if len(original) < 4:
        raise ValueError("mat0.dat shorter than its count")
    count = struct.unpack_from("<i", original, 0)[0]
    if count < 0 or 4 + 8 * count > len(original):
        raise ValueError("mat0.dat count past the file")
    out = bytearray(original)
    for i in range(count):
        struct.pack_into("<i", out, 8 + 8 * i, level)
    return bytes(out)


def gau(original, scenario):
    if len(original) != GAU_SIZE:
        raise ValueError("a panel layout is 0x6a4 bytes")
    target, source, delta = GAU_FIELDS[scenario]
    out = bytearray(original)
    struct.pack_into("<i", out, target, struct.unpack_from("<i", original, source)[0] + delta)
    return bytes(out)


def flight_model(original, scenario):
    if len(original) != FM_SIZE:
        raise ValueError("a flight model is 0x36 bytes")
    out = bytearray(original)
    top, bottom = struct.unpack_from("<ii", original, 0x2a)
    delta = FM_FIELDS[scenario]
    if delta is None:
        struct.pack_into("<ii", out, 0x2a, 0, bottom - top)
    else:
        struct.pack_into("<i", out, 0x2e, top + delta)
    return bytes(out)


def vehicle(original, scenario):
    """A vehicle configuration (dat.md, 0xd8 bytes) with the scenario's word 0."""
    if len(original) != VEHICLE_SIZE:
        raise ValueError("a vehicle configuration is 0xd8 bytes")
    out = bytearray(original)
    struct.pack_into("<h", out, VEHICLE_FIELDS[scenario], 0)
    return bytes(out)


def _loose_names(directory, extension):
    """The members with the extension; for .DAT the configurations of the hercs that have a panel layout."""
    data = _read(os.path.join(directory, "VOL", "SIMVOL0.VOL"))
    names = [n for n, _ in vol_names(data)]
    if extension == ".DAT":
        return data, [n[:-4] + ".DAT" for n in names if n.upper().endswith(".GAU")]
    return data, [n for n in names if n.upper().endswith(extension)]


def _read(path):
    with open(path, "rb") as f:
        return f.read()


def _zone_path(directory):
    sid = zone_id(_read(os.path.join(directory, "DATA", "script.dat")))
    return sid, os.path.join(directory, "dat", "zone%d.dat" % sid)


def _mat0_path(directory):
    return os.path.join(directory, "dat", "mat0.dat")


def write(scenario, directory):
    if scenario in ZONE_FIELDS:
        field3, field4 = ZONE_FIELDS[scenario]
        sid, path = _zone_path(directory)
        original = vol_member(_read(os.path.join(directory, "VOL", "ZONES.VOL")), "ZONE%d.DAT" % sid)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(zone_dat(original, field4, field3))
        if scenario in LEVELS:
            with open(_mat0_path(directory), "wb") as f:
                f.write(mat0(vol_member(_read(os.path.join(directory, "VOL", "SIMVOL0.VOL")), "MAT0.DAT"),
                             LEVELS[scenario]))
        return path
    if scenario == "ladder":
        script_path = os.path.join(directory, "DATA", "script.dat")
        script = bytearray(_read(script_path))
        zones = _read(os.path.join(directory, "VOL", "ZONES.VOL"))
        sid, top = ladder_zone(zones, len(vol_member(zones, "ZONE%d.DBA" % zone_id(script))))
        struct.pack_into("<h", script, 2, sid)
        with open(script_path, "wb") as f:
            f.write(bytes(script))
        _, path = _zone_path(directory)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(zone_dat(vol_member(zones, "ZONE%d.DAT" % sid), -5000 // top))
        return path
    if scenario in MAP_AXIS or scenario == "map-scale":
        path = os.path.join(directory, "DATA", "script.dat")
        old = _read(path)
        data = map_scale_script(old) if scenario == "map-scale" else map_script(old, MAP_AXIS[scenario])
        with open(path, "wb") as f:
            f.write(data)
        return path
    if scenario in LOOSE:
        sub, extension, patch = LOOSE[scenario]
        vol, names = _loose_names(directory, extension)
        os.makedirs(os.path.join(directory, sub), exist_ok=True)
        for name in names:
            with open(os.path.join(directory, sub, name), "wb") as f:
                f.write(patch(vol_member(vol, name)))
        return os.path.join(directory, sub) + " (%d files)" % len(names)
    if scenario == "cell-anim":
        path = os.path.join(directory, "VOL", "SIMVOL0.VOL")
        backup = path + CELL_ANIM_BACKUP
        if os.path.exists(backup):
            raise FileExistsError(backup)
        vol = _read(path)
        patched, changed = cell_anim_vol(vol)
        os.replace(path, backup)
        with open(path, "wb") as f:
            f.write(patched)
        return path + " (%d members)" % changed
    raise KeyError(scenario)


def remove(scenario, directory):
    if scenario == "cell-anim":
        path = os.path.join(directory, "VOL", "SIMVOL0.VOL")
        if os.path.exists(path + CELL_ANIM_BACKUP):
            os.replace(path + CELL_ANIM_BACKUP, path)
        return path
    if scenario in LOOSE:
        sub, extension, _ = LOOSE[scenario]
        _, names = _loose_names(directory, extension)
        for name in names:
            path = os.path.join(directory, sub, name)
            if os.path.exists(path):
                os.remove(path)
        return os.path.join(directory, sub)
    if scenario not in ZONE_FIELDS and scenario != "ladder":
        raise KeyError("only a zone or panel scenario adds files")
    _, path = _zone_path(directory)
    for each in (path, _mat0_path(directory)) if scenario in LEVELS else (path,):
        if os.path.exists(each):
            os.remove(each)
    return path


def main(argv):
    if len(argv) < 2 or argv[0] not in ("write", "remove", "replay") or argv[1] not in SCENARIOS:
        print(__doc__, file=sys.stderr)
        return 2
    command, scenario = argv[0], argv[1]
    if command == "replay":
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import replay_suite
        prepare = replay_suite.prepare_workdir

        def prepare_with_scenario(install, *args, **kwargs):
            result = prepare(install, *args, **kwargs)
            print("scenario %s: %s" % (scenario, write(scenario, install)), flush=True)
            return result
        replay_suite.prepare_workdir = prepare_with_scenario
        return replay_suite.main(argv[2:])
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    print((write if command == "write" else remove)(scenario, argv[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
