"""Do the shipped vehicle tables give vehicle_part and condition_table
non-zero denominators inside their bounded domains?

vehicle_part 417de4 (core/src/damage.c, weapons.md "Domain") keeps one
ratio per equipment entry (portable domain 0..59 entries, the GL count),
four attachment ratios (count 0..4, the DAT word +0x48 that becomes
configuration +0x4a) and divides by the capacities M of the vehicle's DMG
table: M[4], M[5], M[8], M[9] always reachable, M[0] and M[1] unless the
DAT word +0x4e (configuration +0x50) is set, M[10] and M[11] when the count
is above 2. Each equipment entry goes through part_ratio 40dbc0 on region
b + 19 (b = the GL slot byte), whose maximum is the region threshold plus
the capacities its links name; weapon_fit (mission_load.md) replaces that
region with the WEAPONS.DAT record prefix, link 0's capacity index with
b + 12 and M[b + 12] with the record's tail word +8, so a mounted item's
maximum is its threshold plus that word (plus any further links).

condition_table 4151a4 (vehicle.md) divides by the thresholds of regions
0..18 and by sx(M[12 + i]) + threshold(19 + i) for i = 0..9, which is the
DMG pair for an empty slot and the item's tail word plus threshold for a
mounted one.

The mounted items are every non-zero item word of the shipped loadouts:
MSN list 12 (+0x32, ten words; vshell_preparation.md 0x41cbb1), the shell
INI_*, HERCS and TRN_HERC records, the shell's initial inventory, and the
player.mec records of the tapes. The shell's purchasable items (the
availability byte of GAM\\WEAPONS.DAT) are reported separately.
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cd_corpus as cd

EQUIPMENT_LIMIT = 59
ATTACHMENT_LIMIT = 4
ALWAYS = (4, 5, 8, 9)
UNLESS_FLAG = (0, 1)
ABOVE_TWO = (10, 11)
CONDITION_REGIONS = 19
SLOTS = 10


def shell_weapons(data):
    """(id, name, availability) of the shell's GAM\\WEAPONS.DAT and the
    item IDs of its inventory entries (docs/formats/dat.md)."""
    count = cd.i16(data, 0)
    offset = 2
    out = []
    for _ in range(count):
        ident = cd.i16(data, offset)
        length = cd.i16(data, offset + 2)
        name = cd.take(data, offset + 4, length).split(b"\0")[0].decode("latin-1")
        out.append((ident, name, cd.u8(data, offset + 6 + length)))
        offset += 9 + length
    entries = cd.i16(data, offset)
    inventory = [cd.i16(data, offset + 2 + 6 * j) for j in range(entries)]
    if offset + 2 + 6 * entries != len(data):
        raise cd.FormatError("shell WEAPONS.DAT does not end after its inventory")
    return out, inventory


def mounted_items(root):
    """{item id: [where]} over every shipped loadout."""
    found = {}

    def note(item, where):
        if item > 0:
            found.setdefault(item, []).append(where)

    for label, data in cd.entries(root, cd.suffix(".MSN")):
        for index, record in enumerate(cd.msn_lists(data)[11]):
            if record is None:
                continue
            for j in range(10):
                note(cd.i16(record, 0x32 + 2 * j), f"{label} list 12 record {index}")
    for label, data in cd.entries(root, lambda n: n.startswith("INI_") and n.endswith(".DAT")
                                  or n in ("TRN_HERC.DAT", "HERCS.DAT")):
        offset = 0
        if label.upper().endswith("HERCS.DAT"):
            count = cd.i16(data, 0)
            offset = 2
            records = []
            for _ in range(count):
                records.append(cd.loadout_record(data, offset + 2))
                offset = records[-1][2]
        else:
            records = []
            while offset < len(data):
                records.append(cd.loadout_record(data, offset))
                offset = records[-1][2]
        for vehicle, slots, _ in records:
            for _, item in slots:
                note(item, label)
    for label, data in cd.entries(root, cd.suffix(".TAP")):
        records, _ = cd.mec_records(cd.tap_records(data)[1])
        for k, (_, _, _, items) in enumerate(records):
            for item in items:
                note(item, f"{label} player.mec record {k}")
    return found


def scan(root):
    sim = cd.members_by_path(root, "VOL/SIMVOL0.VOL")
    shell = cd.members_by_path(root, "VOL/SHELL0.VOL")
    weapons = cd.sim_weapons(sim["DAT\\WEAPONS.DAT"])
    names, inventory = shell_weapons(shell["GAM\\WEAPONS.DAT"])
    mounted = mounted_items(root)
    for item in inventory:
        mounted.setdefault(item, []).append("VOL/SHELL0.VOL:GAM\\WEAPONS.DAT inventory")
    result = {"vehicles": {}, "items": {}, "violations": []}

    def bad(where, case, **detail):
        result["violations"].append(dict(where=where, case=case, **detail))

    item_maximum = {}
    for ident, record in enumerate(weapons):
        links = record["links"]
        total = record["threshold"] + sum(record["capacity"] if k == 0 else 0
                                          for k in range(len(links)))
        item_maximum[ident] = total
        result["items"][ident] = {
            "name": names[ident][1] if ident < len(names) else None,
            "purchasable": bool(names[ident][2]) if ident < len(names) else None,
            "threshold": record["threshold"], "capacity": record["capacity"],
            "links": links, "part_ratio_maximum": total,
            "mounted": sorted(set(mounted.get(ident, [])))[:5],
            "mounted_count": len(mounted.get(ident, [])),
        }
    for item in sorted(mounted):
        if item >= len(weapons):
            bad("loadouts", "item outside WEAPONS.DAT", item=item)
        elif item_maximum[item] == 0:
            bad("loadouts", "mounted item with maximum 0", item=item,
                where_mounted=mounted[item][:5])
    for item, info in result["items"].items():  # item 0 is the empty word
        if item and info["part_ratio_maximum"] == 0 and info["purchasable"]:
            bad("VOL/SHELL0.VOL:GAM\\WEAPONS.DAT", "purchasable item with maximum 0", item=item)

    for path in sorted(sim):
        if not (path.startswith("DAT\\") and len(sim[path]) == 216):
            continue
        stem = path[4:-4]
        dat = sim[path]
        caps, regions = cd.dmg_file(sim[f"DMG\\{stem}.DMG"])
        gun = cd.gun_list(sim[f"GL\\{stem}.GL"])
        count = cd.i16(dat, 0x48)
        flag = cd.i16(dat, 0x4e)
        where = f"VOL/SIMVOL0.VOL:{stem}"
        used = list(ALWAYS) + ([] if flag else list(UNLESS_FLAG)) + (list(ABOVE_TWO) if count > 2 else [])
        entry = {"attachments": count, "flag_4e": flag, "equipment": len(gun),
                 "capacities": caps, "used_capacities": sorted(used),
                 "condition_thresholds": [r["threshold"] for r in regions[:CONDITION_REGIONS]],
                 "empty_slot_divisors": []}
        if not 0 <= count <= ATTACHMENT_LIMIT:
            bad(where, "attachment count", count=count)
        if len(gun) > EQUIPMENT_LIMIT:
            bad(where, "equipment count", count=len(gun))
        for index in used:
            if index >= len(caps) or caps[index] == 0:
                bad(where, "used capacity 0", index=index)
        if len(regions) < CONDITION_REGIONS + SLOTS or len(caps) < 12 + SLOTS:
            bad(where, "table too short for condition_table", regions=len(regions), capacities=len(caps))
        else:
            for i, region in enumerate(regions[:CONDITION_REGIONS]):
                if region["threshold"] == 0:
                    bad(where, "condition threshold 0", region=i)
            for i in range(SLOTS):
                divisor = caps[12 + i] + regions[19 + i]["threshold"]
                entry["empty_slot_divisors"].append(divisor)
                if divisor == 0:
                    bad(where, "empty slot divisor 0", slot=i)
        result["vehicles"][stem] = entry
    result["maximum_attachments"] = max(v["attachments"] for v in result["vehicles"].values())
    result["maximum_equipment"] = max(v["equipment"] for v in result["vehicles"].values())
    result["mounted_items"] = sorted(mounted)
    result["zero_maximum_items"] = sorted(i for i, m in item_maximum.items() if m == 0)
    result["holds"] = bool(result["vehicles"]) and not result["violations"]
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("root", nargs="?", help="CD root (default ES2_DATA_ROOT)")
    parser.add_argument("--out", help="write the JSON evidence here")
    args = parser.parse_args(argv)
    result = scan(cd.data_root(args.root))
    text = json.dumps(result, indent=1)
    if args.out:
        Path(args.out).write_text(text + "\n", encoding="utf-8")
    print(json.dumps({k: result[k] for k in ("holds", "maximum_attachments", "maximum_equipment",
                                             "mounted_items", "zero_maximum_items", "violations")}))
    return 0 if result["holds"] else 1


if __name__ == "__main__":
    sys.exit(main())
