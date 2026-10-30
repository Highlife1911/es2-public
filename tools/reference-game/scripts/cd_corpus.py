"""Read-only access to the shipped CD data for the data-domain scans.

The root is the verified extraction (ES2_DATA_ROOT, or a path argument).
Every file whose first four bytes are VOLN is read as an archive
(docs/formats/vol.md); a member is yielded under the path
"<archive relative path>:<directory><name>" with its payload, so the
loose files and the VOL members of one name can be told apart.
"""

import os
import struct
from pathlib import Path


class FormatError(ValueError):
    pass


def u8(data, offset):
    if offset < 0 or offset + 1 > len(data):
        raise FormatError(f"u8 at {offset:#x} beyond {len(data):#x}")
    return data[offset]


def i8(data, offset):
    value = u8(data, offset)
    return value - 0x100 if value >= 0x80 else value


def u16(data, offset):
    if offset < 0 or offset + 2 > len(data):
        raise FormatError(f"u16 at {offset:#x} beyond {len(data):#x}")
    return struct.unpack_from("<H", data, offset)[0]


def i16(data, offset):
    if offset < 0 or offset + 2 > len(data):
        raise FormatError(f"i16 at {offset:#x} beyond {len(data):#x}")
    return struct.unpack_from("<h", data, offset)[0]


def u32(data, offset):
    if offset < 0 or offset + 4 > len(data):
        raise FormatError(f"u32 at {offset:#x} beyond {len(data):#x}")
    return struct.unpack_from("<I", data, offset)[0]


def take(data, offset, length):
    if offset < 0 or length < 0 or offset + length > len(data):
        raise FormatError(f"{length} bytes at {offset:#x} beyond {len(data):#x}")
    return data[offset:offset + length]


class VolMember:
    __slots__ = ("directory", "name", "kind", "block", "length", "archive")

    def __init__(self, directory, name, kind, block, length, archive):
        self.directory = directory
        self.name = name
        self.kind = kind
        self.block = block
        self.length = length
        self.archive = archive

    @property
    def path(self):
        return self.directory + self.name

    def payload(self):
        return take(self.archive, self.block + 9, self.length)


def vol_members(data):
    """The member records of a VOLN archive, in table order."""
    if take(data, 0, 4) != b"VOLN":
        raise FormatError("no VOLN signature")
    directory_count = u8(data, 9)
    directory_bytes = u16(data, 10)
    strings = take(data, 12, directory_bytes).split(b"\0")
    if len(strings) < directory_count:
        raise FormatError("directory strings short")
    directories = [s.decode("ascii") for s in strings[:directory_count]]
    count = u16(data, 12 + directory_bytes)
    table = 18 + directory_bytes
    members = []
    for index in range(count):
        record = table + 18 * index
        name = take(data, record, 13).split(b"\0")[0].decode("ascii")
        directory_index = u8(data, record + 13)
        if directory_index >= directory_count:
            raise FormatError(f"member {index}: directory {directory_index}")
        block = u32(data, record + 14)
        kind = u8(data, block)
        length = u32(data, block + 1)
        take(data, block + 9, length)
        members.append(VolMember(directories[directory_index], name, kind,
                                 block, length, data))
    return members


def data_root(argument=None):
    root = argument or os.environ.get("ES2_DATA_ROOT")
    if not root:
        raise SystemExit("no data root: pass a path or set ES2_DATA_ROOT")
    path = Path(root)
    if not (path / "VOL").is_dir() and (path / "extracted" / "cd").is_dir():
        path = path / "extracted" / "cd"
    if not path.is_dir():
        raise SystemExit(f"data root {path} is not a directory")
    return path


def _relative(root, path):
    return path.relative_to(root).as_posix()


def files(root):
    """Every regular file under root, sorted by relative path."""
    root = Path(root)
    return sorted((p for p in root.rglob("*") if p.is_file()),
                  key=lambda p: _relative(root, p).upper())


def is_voln(path):
    with open(path, "rb") as handle:
        return handle.read(4) == b"VOLN"


def archives(root):
    """(relative path, bytes, members) of every VOLN archive under root."""
    root = Path(root)
    for path in files(root):
        if is_voln(path):
            data = path.read_bytes()
            yield _relative(root, path), data, vol_members(data)


def entries(root, predicate):
    """(label, bytes) of every loose file and VOL member whose name
    (upper-case, without directory) satisfies predicate."""
    root = Path(root)
    for path in files(root):
        if predicate(path.name.upper()) and not is_voln(path):
            yield _relative(root, path), path.read_bytes()
    for label, _, members in archives(root):
        for member in members:
            if predicate(member.name.upper()):
                yield f"{label}:{member.path}", member.payload()


def suffix(*suffixes):
    wanted = tuple(s.upper() for s in suffixes)
    return lambda name: name.endswith(wanted)


def str_groups(data):
    """The groups of a .STR file (docs/formats/str.md): u32 body size B,
    then groups of i16 N strings until the body is used up; a string is
    i16 L, L bytes, byte E, E aux bytes. Each group is a list of
    (text without its NUL, aux bytes)."""
    end = 4 + u32(data, 0)
    if end > len(data):
        raise FormatError(f"body ends at {end:#x} beyond {len(data):#x}")
    offset = 4
    groups = []
    while offset < end:
        count = i16(data, offset)
        if count < 0:
            raise FormatError(f"negative string count at {offset:#x}")
        offset += 2
        group = []
        for _ in range(count):
            length = i16(data, offset)
            if length < 1:
                raise FormatError(f"string length {length} at {offset:#x}")
            text = take(data, offset + 2, length)
            offset += 2 + length
            extra = u8(data, offset)
            aux = take(data, offset + 1, extra)
            offset += 1 + extra
            group.append((text.split(b"\0")[0], aux))
        groups.append(group)
    if offset != end:
        raise FormatError(f"last group ends at {offset:#x}, body at {end:#x}")
    return groups


def tap_records(data, count=7):
    """The first count length-prefixed records of a .TAP file
    (docs/reverse-engineering/file_io.md, "Tape header")."""
    offset = 0
    out = []
    for _ in range(count):
        length = u32(data, offset)
        out.append(take(data, offset + 4, length))
        offset += 4 + length
    return out


def members_by_path(root, archive):
    """{upper-case directory + name: payload} of one archive under root
    (archive is its relative path, e.g. "VOL/SIMVOL0.VOL")."""
    for label, _, members in archives(root):
        if label.upper() == archive.upper():
            return {m.path.upper(): m.payload() for m in members}
    raise SystemExit(f"{archive} missing below {root}")


def dmg_file(data):
    """A vehicle damage table dmg\\*.DMG (docs/formats/collision.md): the
    capacities and the regions as dicts (threshold, debris, cell, parent,
    flags, links [(weight, capacity index)])."""
    count = i16(data, 0)
    if count < 0:
        raise FormatError(f"capacity count {count}")
    capacities = [i16(data, 2 + 2 * i) for i in range(count)]
    offset = 2 + 2 * count
    region_count = i16(data, offset)
    if region_count < 0:
        raise FormatError(f"region count {region_count}")
    offset += 2
    regions = []
    for _ in range(region_count):
        links = i16(data, offset + 6)
        if links < 0:
            raise FormatError(f"link count {links} at {offset:#x}")
        regions.append({
            "threshold": i16(data, offset),
            "debris": i8(data, offset + 2),
            "cell": i8(data, offset + 3),
            "parent": i8(data, offset + 4),
            "flags": u8(data, offset + 5),
            "links": [(i16(data, offset + 8 + 4 * k), i16(data, offset + 10 + 4 * k))
                      for k in range(links)],
        })
        offset += 8 + 4 * links
    if offset != len(data):
        raise FormatError(f"regions end at {offset:#x} of {len(data):#x}")
    return capacities, regions


def sim_weapons(data):
    """The simulator's dat\\WEAPONS.DAT (docs/formats/dat.md): per record
    the region prefix (threshold, links [(weight, capacity index)]) that
    weapon_fit installs and the tail word +8 it stores as the capacity."""
    count = i16(data, 0)
    offset = 2
    out = []
    for _ in range(count):
        threshold = i16(data, offset)
        links = i16(data, offset + 6)
        if links < 0:
            raise FormatError(f"link count {links} at {offset:#x}")
        pairs = [(i16(data, offset + 8 + 4 * k), i16(data, offset + 10 + 4 * k))
                 for k in range(links)]
        b = i16(data, offset + 10 + 4 * links)
        if b < 0:
            raise FormatError(f"B {b} at {offset:#x}")
        tail = offset + 12 + 4 * links + 8 * b
        out.append({"threshold": threshold, "links": pairs,
                    "capacity": i16(data, tail + 8)})
        offset = tail + 48
    if offset != len(data):
        raise FormatError(f"records end at {offset:#x} of {len(data):#x}")
    return out


def gun_list(data):
    """A gl\\*.GL hardpoint list: (category i8 +6, slot i8 +0x17) per
    26-byte record after the i16 count."""
    count = i16(data, 0)
    if count < 0:
        raise FormatError(f"count {count}")
    take(data, 2, 26 * count)
    if 2 + 26 * count != len(data):
        raise FormatError(f"{count} records leave {len(data) - 2 - 26 * count} bytes")
    return [(i8(data, 2 + 26 * j + 6), i8(data, 2 + 26 * j + 0x17)) for j in range(count)]


MSN_STRIDES = (14, 82, 8, 144, 64, 22, 10, 0, 12, 82, 30, 144, 102, 62, 22, 164, 58)


def msn_lists(data):
    """The 17 record lists of a version-5 MSN (formats/src/msn.c); list 7
    (routes) is variable. A record cut short by the end of the data is
    None (DEMO2's last objective, docs/formats/mission.md)."""
    if i16(data, 0) != 5:
        raise FormatError("MSN version is not 5")
    offset = 2
    lists = []
    for stride in MSN_STRIDES:
        count = i16(data, offset)
        if count < 0:
            raise FormatError(f"list count {count} at {offset:#x}")
        offset += 2
        records = []
        for _ in range(count):
            if stride == 0:
                size = 10 + 2 * i16(data, offset + 8)
            else:
                size = stride
            records.append(data[offset:offset + size] if offset + size <= len(data) else None)
            offset += size
        lists.append(records)
    return lists


def loadout_record(data, offset):
    """A shell loadout record (docs/formats/dat.md): (vehicle, [(slot,
    item)], end offset)."""
    vehicle = i16(data, offset)
    count = i16(data, offset + 6)
    if count < 0:
        raise FormatError(f"slot count {count} at {offset:#x}")
    slots = [(i16(data, offset + 8 + 8 * j), i16(data, offset + 10 + 8 * j))
             for j in range(count)]
    return vehicle, slots, offset + 8 + 8 * count


def mec_records(data):
    """A player.mec (docs/reverse-engineering/vshell_preparation.md,
    0x40f68c): (variant, rank, vehicle, items) per record after the zero
    word and the i16 count; each record is 76 + 4K bytes."""
    count = i16(data, 2)
    offset = 4
    out = []
    for _ in range(count):
        k = i16(data, offset + 6)
        if k < 0:
            raise FormatError(f"capacity {k} at {offset:#x}")
        out.append((i16(data, offset), i16(data, offset + 2), i16(data, offset + 4),
                    [i16(data, offset + 8 + 2 * j) for j in range(k)]))
        offset += 76 + 4 * k
    return out, offset
