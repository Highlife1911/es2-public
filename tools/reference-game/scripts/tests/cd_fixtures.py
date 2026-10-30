"""Synthetic CD trees for the data-domain scan tests."""
import struct
from pathlib import Path


def vol(members, kinds=None):
    """A VOLN archive of {"DIR\\NAME": payload} (docs/formats/vol.md)."""
    kinds = kinds or {}
    directories = sorted({path.rsplit("\\", 1)[0] + "\\" for path in members})
    strings = b"".join(d.encode("ascii") + b"\0" for d in directories)
    table = 18 + len(strings)
    blocks = table + 18 * len(members)
    header = bytearray(b"VOLN" + bytes(5) + bytes([len(directories)])
                       + struct.pack("<H", len(strings)) + strings
                       + struct.pack("<H", len(members)) + bytes(4))
    records = bytearray()
    body = bytearray()
    for path, payload in members.items():
        directory, name = path.rsplit("\\", 1)
        records += name.encode("ascii").ljust(13, b"\0")
        records += bytes([directories.index(directory + "\\")])
        records += struct.pack("<I", blocks + len(body))
        body += bytes([kinds.get(path, 2)]) + struct.pack("<I", len(payload)) + bytes(4) + payload
    return bytes(header + records + body)


def tree(root, files):
    """Write {relative path: bytes} below root."""
    root = Path(root)
    for relative, data in files.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    return root


def words(*values):
    return struct.pack("<%dh" % len(values), *values)


def gun_list(slots):
    out = bytearray(words(len(slots)))
    for slot in slots:
        record = bytearray(26)
        record[0x17] = slot & 0xff
        out += record
    return bytes(out)


def dmg(capacities, regions):
    """regions: [(threshold, [(weight, capacity index)])]."""
    out = bytearray(words(len(capacities), *capacities) + words(len(regions)))
    for threshold, links in regions:
        out += words(threshold) + bytes(4) + words(len(links))
        for weight, index in links:
            out += words(weight, index)
    return bytes(out)


def sim_weapons(records):
    """records: [(threshold, [(weight, index)], tail word +8)]."""
    out = bytearray(words(len(records)))
    for threshold, links, capacity in records:
        out += words(threshold, 0, 0, len(links))
        for weight, index in links:
            out += words(weight, index)
        out += words(0, 0)
        tail = bytearray(48)
        tail[8:10] = words(capacity)
        out += tail
    return bytes(out)


def vehicle_dat(attachments, flag=0):
    data = bytearray(216)
    data[0x48:0x4a] = words(attachments)
    data[0x4e:0x50] = words(flag)
    return bytes(data)


def shell_weapons(items, inventory):
    """items: [(id, name, availability)]."""
    out = bytearray(words(len(items)))
    for ident, name, available in items:
        text = name.encode("ascii") + b"\0"
        out += words(ident, len(text)) + text + words(0) + bytes([available]) + words(0)
    out += words(len(inventory))
    for item in inventory:
        out += words(item, 100, 5)
    return bytes(out)


def mec(records):
    """records: [(identity, rank, vehicle, items)]."""
    out = bytearray(words(0, len(records)))
    for identity, rank, vehicle, items in records:
        out += words(identity, rank, vehicle, len(items), *items)
        out += words(*([5] * len(items))) + words(0) + words(*([100] * 33))
    out += words(33) + bytes(33)
    return bytes(out)


def tap(records):
    return b"".join(struct.pack("<I", len(r)) + r for r in records)


def str_file(groups):
    """groups: [[(text, aux)]]."""
    body = bytearray()
    for group in groups:
        body += words(len(group))
        for text, aux in group:
            body += words(len(text) + 1) + text + b"\0" + bytes([len(aux)]) + aux
    return struct.pack("<I", len(body)) + bytes(body)
