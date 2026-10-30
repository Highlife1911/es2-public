# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""build_site.py on the synthetic CD of testdata/site/cd_tree.json.

usage: test_build_site.py <build_site.py> <zstd_pack.exe> <cd_tree.json>

Checks the exclusions, the two trees and the language entries, the VOL split (member parts and
one rest part covering the file exactly), every part decoded by zstd_pack against its hash and
its file ranges, deduplication, media entries without bytes, the copied media, CD music and
root files with the absent list, the self-test page only with --selftest, an equal tree hash for
two builds, the size limits, and that --out is never cleared unless it holds an earlier site.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

SCRIPT, ZSTD_PACK, TREE = (Path(a).resolve() for a in sys.argv[1:4])
spec = importlib.util.spec_from_file_location("build_site", SCRIPT)
site = importlib.util.module_from_spec(spec)
spec.loader.exec_module(site)
checks = 0


def check(value, detail=""):
    global checks
    checks += 1
    if not value:
        raise AssertionError(detail)


def raises(fn, text):
    try:
        fn()
    except site.BuildError as e:
        check(text in str(e), f"expected '{text}', got '{e}'")
        return
    check(False, f"no BuildError ('{text}')")


def noise(n, seed):
    state, out = seed & 0x7FFFFFFF, bytearray()
    for _ in range(n):
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        out.append((state >> 13) & 0xFF)
    return bytes(out)


def vol_archive(members, content):
    """A VOLN archive (docs/formats/vol.md) of [directory, name, content spec] members."""
    directories = sorted({d for d, _, _ in members}, key=str.upper)
    strings = b"".join(d.encode("ascii") + b"\0" for d in directories)
    head = 18 + len(strings) + 18 * len(members)
    records, blocks, at = [], [], head
    for directory, name, spec in members:
        payload = content(spec, name)
        records.append(name.encode("ascii").ljust(13, b"\0") + bytes([directories.index(directory)]) + struct.pack("<I", at))
        blocks.append(b"\2" + struct.pack("<I", len(payload)) + b"meta" + payload + payload[-1:])
        at += len(payload) + 10
    return (b"VOLN\1\0\0\0\5" + bytes([len(directories)]) + struct.pack("<H", len(strings)) + strings
            + struct.pack("<HI", len(members), 18 * len(members)) + b"".join(records) + b"".join(blocks))


def write_cd(root, tree):
    files = tree["files"]
    made = {}

    def content(spec, name):
        if "text" in spec:
            return spec["text"].encode("latin-1")
        if "noise" in spec:
            return noise(spec["noise"], int(hashlib.sha256(name.encode()).hexdigest()[:8], 16))
        if "copy" in spec:
            return made.get(spec["copy"]) or content(files[spec["copy"]], spec["copy"])
        return vol_archive(spec["vol"], content)

    for name, spec in files.items():
        data = content(spec, name)
        made[name] = data
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    return made


def decode(part_file, work):
    out = work / "decoded.bin"
    subprocess.run([str(ZSTD_PACK), "-d", str(part_file), str(out)], check=True)
    return out.read_bytes()


tree = json.loads(TREE.read_text(encoding="utf-8"))
with tempfile.TemporaryDirectory(dir=os.environ.get("TEST_TMPDIR")) as tmp:
    tmp = Path(tmp)
    cd = tmp / "cd"
    made = write_cd(cd, tree)
    media = tmp / "media"
    for name, body in (("c1.webm", b"v"), ("c1.audio.webm", b"a"), ("avf/intr_pt1.audio.webm", b"f"),
                       ("manifest.json", b"{}"), ("report.txt", b"x")):
        (media / name).parent.mkdir(parents=True, exist_ok=True)
        (media / name).write_bytes(body)
    audio = tmp / "audio"
    audio.mkdir()
    for name in site.CD_AUDIO_FILES:
        (audio / name).write_bytes(name.encode())
    root_files = [tmp / "vfs.js", tmp / "vfs.wasm"]
    for f in root_files:
        f.write_bytes(f.name.encode())

    out = tmp / "site"
    report = site.build(cd, out, ZSTD_PACK, media, audio, root_files, jobs=3)
    manifest = json.loads((out / "manifest.json").read_text(encoding="utf-8"))
    files = {f["path"]: f for f in manifest["files"]}

    # The trees.
    check(report["excluded"] == tree["excluded"], f"excluded: {report['excluded']}")
    expected_d = {"D:\\" + n.replace("/", "\\") for n in made
                  if not any(n == e or n.startswith(e + "/") for e in tree["excluded"])}
    expected_c = {"C:\\ES2\\" + n.replace("/", "\\") for n in made if n.split("/")[0] in site.INSTALL_DIRS}
    check(set(files) == expected_d | expected_c, f"paths: {sorted(set(files) ^ (expected_d | expected_c))}")
    check(all(files[p]["size"] == len(made[p[3:].replace("\\", "/")]) for p in expected_d), "D:\\ sizes")
    check("C:\\ES2\\ERROR.STR" not in files and not any("GERMAN" in p for p in expected_c), "no language files in C:\\ES2")
    check({"C:\\ES2", "C:\\ES2\\VOL", "D:\\", "D:\\GERMAN\\BILLBRD"} <= set(manifest["directories"]), "directories")
    check(not any(d.upper().startswith(("D:\\DEMOS", "D:\\AUTORUN")) for d in manifest["directories"]), "excluded directories")

    # Media entries.
    for p in ("D:\\AVI\\C1.AVI", "D:\\AVF\\INTR_PT1.AVI", "D:\\AVG\\INTR_PT1.AVI"):
        check(files[p]["flags"] == ["media", "no-bytes"] and files[p]["parts"] == [] and files[p]["size"] > 0, p)
    check(report["media_entries"] == 3, "media count")

    # Every part decodes to its hash and its file ranges; the parts cover each file exactly.
    data_files = {p.name for p in (out / "data").iterdir()}
    shas = set()
    for path, entry in files.items():
        if "no-bytes" in entry["flags"]:
            continue
        source = made[path[3:].replace("\\", "/")] if path.startswith("D:") else made[path[7:].replace("\\", "/")]
        covered = []
        for part in entry["parts"]:
            ranges = part.get("ranges") or [[part["offset"], part["length"]]]
            check(("offset" in part) != ("ranges" in part), f"{path}: offset or ranges")
            raw = decode(out / "data" / f"{part['sha256']}.zst", tmp)
            check(hashlib.sha256(raw).hexdigest() == part["sha256"] and len(raw) == part["length"], f"{path}: part hash")
            check(raw == b"".join(source[o:o + n] for o, n in ranges), f"{path}: part bytes")
            check(part["compressed"] == (out / "data" / f"{part['sha256']}.zst").stat().st_size, f"{path}: compressed")
            covered += ranges
            shas.add(part["sha256"])
        covered.sort()
        at = 0
        for o, n in covered:
            check(o == at and n > 0, f"{path}: ranges {covered}")
            at = o + n
        check(at == len(source), f"{path}: covered {at} of {len(source)}")
    check(data_files == {f"{s}.zst" for s in shas} and report["parts"] == len(shas), "one data file per part")

    # The VOL split and deduplication.
    shell0, shell1 = files["D:\\VOL\\SHELL0.VOL"], files["D:\\VOL\\SHELL1.VOL"]
    check(shell0["flags"] == ["vol"] and len(shell0["parts"]) == 4 and len(shell1["parts"]) == 4, "member parts plus one rest part")
    check(len(shell0["parts"][-1]["ranges"]) == 4 and shell0["parts"][-1]["ranges"][0][0] == 0, "the rest: header and block bytes")
    check(files["C:\\ES2\\VOL\\SHELL0.VOL"]["parts"] == shell0["parts"], "the installation copy shares the parts")
    voice = files["D:\\SIMVOICE\\TM1_0001.WAV"]["parts"][0]["sha256"]
    check(files["D:\\SIMVOICF\\TM1_0001.WAV"]["parts"][0]["sha256"] == voice
          and files["D:\\SIMVOICG\\TM1_0001.WAV"]["parts"][0]["sha256"] == voice, "the voice folders share a part")
    members0 = {p["sha256"] for p in shell0["parts"][:-1]}
    members1 = {p["sha256"] for p in shell1["parts"][:-1]}
    check(voice in members0 and voice in members1, "a VOL member equal to a loose file shares its part")
    check(files["D:\\TAPES\\DEMO1.TAP"]["parts"][0]["sha256"] in members1, "a member equal to another loose file")
    check(files["D:\\EMPTY.TXT"]["parts"] == [] and files["D:\\EMPTY.TXT"]["size"] == 0, "an empty file has no part")
    check(report["part_references"] > report["parts"], "references exceed stored parts")

    # The language entries (what prepare-native.ps1 -Language writes).
    german = manifest["languages"]["G"]
    check(["C:\\ES2\\ERROR.STR", "D:\\GERMAN\\ERROR.GER"] in german["copies"]
          and ["C:\\ES2\\DATA\\MISSION.STR", "D:\\GERMAN\\MISSION.GER"] in german["copies"]
          and ["C:\\ES2\\GERMAN\\BILLBRD\\LOAD1.BMP", "D:\\GERMAN\\BILLBRD\\LOAD1.BMP"] in german["copies"], "German copies")
    check(not any("ES2GUIDE" in c[0] for c in german["copies"]), "no help file in the language copy")
    check(german["directories"] == ["C:\\ES2\\GERMAN", "C:\\ES2\\GERMAN\\BILLBRD"], f"{german['directories']}")
    check(german["writes"] == {"C:\\ES2\\DATA\\LANGUAGE.CFG": "G", "C:\\ES2\\DATA\\DRIVE.CFG": "D:\r\nC:\\ES2\r\n"}, "writes")
    check(sorted(manifest["languages"]) == ["E", "F", "G"], "three languages")
    check(all(c[1] in files for lang in manifest["languages"].values() for c in lang["copies"]), "copies name base files")

    # Media, CD music and root files.
    check(report["media"] == ["avf/intr_pt1.audio.webm", "c1.audio.webm", "c1.webm", "manifest.json"]
          and report["media_skipped"] == ["report.txt"], f"media {report['media']} {report['media_skipped']}")
    check((out / "media" / "avf" / "intr_pt1.audio.webm").read_bytes() == b"f", "media copied")
    check(sorted(p.name for p in (out / "cd").iterdir()) == sorted(site.CD_AUDIO_FILES), "CD music copied")
    check((out / "vfs.wasm").read_bytes() == b"vfs.wasm", "root file copied")
    check("vfs module" not in report["absent"] and "es2sim module" in report["absent"], f"absent {report['absent']}")

    # Determinism: another build, and a rebuild in place.
    other = site.build(cd, tmp / "site2", ZSTD_PACK, media, audio, root_files, jobs=1)
    check(other["tree_sha256"] == report["tree_sha256"], "two builds, one tree hash")
    again = site.build(cd, out, ZSTD_PACK, media, audio, root_files, jobs=2, clean=True)
    check(again["tree_sha256"] == report["tree_sha256"], "a rebuild in place")
    check(again["site_bytes"] == sum(p.stat().st_size for p in out.rglob("*") if p.is_file()), "site size")
    bare = site.build(cd, tmp / "site3", ZSTD_PACK)
    check(bare["tree_sha256"] != report["tree_sha256"] and "media (--media)" in bare["absent"]
          and "CD music (--cd-audio)" in bare["absent"], "a site without media")

    # --out safety.
    raises(lambda: site.build(cd, out, ZSTD_PACK), "not empty")
    foreign = tmp / "foreign"
    foreign.mkdir()
    (foreign / "keep.txt").write_bytes(b"keep")
    raises(lambda: site.build(cd, foreign, ZSTD_PACK, clean=True), "holds no earlier site")
    check((foreign / "keep.txt").read_bytes() == b"keep", "a foreign directory stays")
    raises(lambda: site.build(cd, tmp / "s4", ZSTD_PACK, files=[tmp / "vfs.js", tmp / "vfs.js"]), "given twice")
    renamed = site.build(cd, tmp / "s18", ZSTD_PACK, files=[f"es2shell.wasm={tmp / 'vfs.wasm'}", tmp / "vfs.js"])
    check((tmp / "s18" / "es2shell.wasm").read_bytes() == b"vfs.wasm" and not (tmp / "s18" / "vfs.wasm").exists()
          and "es2shell module" not in renamed["absent"] and renamed["files"] == ["es2shell.wasm", "vfs.js"],
          f"NAME=PATH puts a file under NAME {renamed['files']}")
    raises(lambda: site.build(cd, tmp / "s19", ZSTD_PACK, files=[f"es2shell.wasm={tmp / 'vfs.wasm'}", f"es2shell.wasm={tmp / 'vfs.js'}"]),
           "given twice")

    # The page, and the self-test page only with --selftest.
    page_files = [tmp / name for name in ("index.html", "app.js", "sw.js")]
    selftest_files = [tmp / name for name in site.SELFTEST_FILES]
    for f in page_files + selftest_files:
        f.write_bytes(f.name.encode())
    page = site.build(cd, tmp / "s10", ZSTD_PACK, files=page_files)
    check(not page["selftest"] and not any(r.startswith(("page", "service worker")) for r in page["absent"])
          and not (tmp / "s10" / "selftest.html").exists(), f"the page {page['absent']}")
    for f in selftest_files:
        raises(lambda: site.build(cd, tmp / f"s11{f.stem}", ZSTD_PACK, files=page_files + [f]), "needs --selftest")
    raises(lambda: site.build(cd, tmp / "s12", ZSTD_PACK, files=page_files, selftest=True), "--selftest without")
    local = site.build(cd, tmp / "s13", ZSTD_PACK, files=page_files + selftest_files, selftest=True)
    check(local["selftest"] and (tmp / "s13" / "selftest.html").read_bytes() == b"selftest.html"
          and local["tree_sha256"] != page["tree_sha256"], "the self-test page with --selftest")
    suites = tmp / "selftest_suites.json"
    suites.write_bytes(b"{}")
    raises(lambda: site.build(cd, tmp / "s20", ZSTD_PACK, files=page_files + [suites]), "needs --selftest")
    with_suites = site.build(cd, tmp / "s21", ZSTD_PACK, files=page_files + selftest_files + [suites], selftest=True)
    check(with_suites["selftest"] and (tmp / "s21" / "selftest_suites.json").read_bytes() == b"{}",
          "the suite replays with --selftest")

    # The limits.
    limit = site.MAX_FILE_BYTES
    site.MAX_FILE_BYTES = 4000
    raises(lambda: site.build(cd, tmp / "s5", ZSTD_PACK), "over 4000")
    site.MAX_FILE_BYTES = limit
    limit = site.MAX_SITE_BYTES
    site.MAX_SITE_BYTES = report["site_bytes"]
    raises(lambda: site.build(cd, tmp / "s6", ZSTD_PACK, media, audio, root_files), "not below")
    site.MAX_SITE_BYTES = limit

    # Missing inputs and a broken archive.
    (audio / "track07.opus").unlink()
    raises(lambda: site.build(cd, tmp / "s7", ZSTD_PACK, cd_audio=audio), "no track07.opus")
    broken = bytearray(made["VOL/SHELL1.VOL"])
    broken[-12:] = b""
    (cd / "VOL" / "SHELL1.VOL").write_bytes(bytes(broken))
    raises(lambda: site.build(cd, tmp / "s8", ZSTD_PACK), "beyond the end")
    broken = bytearray(made["VOL/SHELL1.VOL"])
    broken[-1] ^= 1
    (cd / "VOL" / "SHELL1.VOL").write_bytes(bytes(broken))
    raises(lambda: site.build(cd, tmp / "s14", ZSTD_PACK), "no suffix byte")
    (cd / "VOL" / "SHELL1.VOL").write_bytes(made["VOL/SHELL1.VOL"])

    # Prefetch lists from part logs: per mission (or "shell", the shell tour's) the union in first-seen
    # order, logs in path order.
    known = sorted({p["sha256"] for f in files.values() for p in f["parts"]})
    logs = tmp / "logs"
    for name, mission, parts in (("b", "world2-zone123", [known[2], known[0]]), ("a", "world2-zone123", [known[1], known[2]]),
                                 ("c", "world-1-zone7", [known[3]]), ("f", "shell", [known[4], known[0]])):
        (logs / name).mkdir(parents=True)
        (logs / name / "part-log.json").write_text(json.dumps({"mission": mission, "parts": parts}), encoding="utf-8")
    good = sorted(logs.glob("*/part-log.json"))
    pre = site.build(cd, tmp / "s15", ZSTD_PACK, part_logs=good)
    lists = {p.stem: json.loads(p.read_text(encoding="utf-8")) for p in (tmp / "s15" / "prefetch").iterdir()}
    check(lists == {"world2-zone123": {"parts": [known[1], known[2], known[0]]}, "world-1-zone7": {"parts": [known[3]]},
                    "shell": {"parts": [known[4], known[0]]}},
          f"prefetch lists {lists}")
    check(pre["prefetch"] == {"shell": 2, "world-1-zone7": 1, "world2-zone123": 3} and "prefetch lists (--part-log)" not in pre["absent"]
          and "prefetch lists (--part-log)" in bare["absent"], f"prefetch report {pre['prefetch']}")
    (logs / "d.json").write_text(json.dumps({"mission": "world2-zone123", "parts": ["0" * 64]}), encoding="utf-8")
    raises(lambda: site.build(cd, tmp / "s16", ZSTD_PACK, part_logs=good + [logs / "d.json"]), "not in the manifest")
    (logs / "e.json").write_text(json.dumps({"mission": None, "parts": []}), encoding="utf-8")
    raises(lambda: site.build(cd, tmp / "s17", ZSTD_PACK, part_logs=[logs / "e.json"]), "no mission key")
    check(not (tmp / "s16").exists() and not (tmp / "s17").exists(), "a refused part log writes no site")

    # The command line.
    (cd / "VOL" / "SHELL1.VOL").write_bytes(made["VOL/SHELL1.VOL"])
    result = subprocess.run([sys.executable, "-B", str(SCRIPT), "--cd", str(cd), "--out", str(tmp / "s9"),
                             "--zstd-pack", str(ZSTD_PACK), "--report", str(tmp / "r.json")],
                            capture_output=True, text=True)
    check(result.returncode == 0 and f"tree sha256 {bare['tree_sha256']}" in result.stdout, result.stdout + result.stderr)
    check(json.loads((tmp / "r.json").read_text())["tree_sha256"] == bare["tree_sha256"], "the report file")
print(f"{checks} checks passed")
