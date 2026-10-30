"""Builds the web site: the base trees as content-addressed parts and manifest.json.

docs/web_build.md sections 8.1, 8.2 and 17. The trees:

- D:\\ holds the extracted CD without the installers and system components, the help files and
  DEMOS (EXCLUDED_DIRS, EXCLUDED_EXTENSIONS, excluded_name). Every *.AVI keeps an entry with its
  size, flags "media" and "no-bytes" and no parts.
- C:\\ES2\\ holds DATA, SAV, TAPES and VOL copied from the CD (INSTALL_DIRS). The language files
  are not in it: manifest.json's "languages" gives, per language byte, what the landing page
  writes into the overlay (directories, copies of D:\\ files as part references, and the two
  configuration files with their text).

Parts are data/<sha256>.zst, one zstd frame (//tools/web:zstd_pack, level ZSTD_LEVEL) per part.
A file is one part; a VOLN archive (docs/formats/vol.md) is one part per member payload and one
part for the rest (header, directory, member table and the ten bytes around each payload), whose
"ranges" list the file ranges the part's bytes come from in order. Identical parts are stored once.

Further content, each optional: --media copies <name>.webm, <name>.audio.webm and manifest.json
(with avf\\ and avg\\) into media/; --cd-audio copies track02..07.opus and toc.json into cd/; every
--file is copied into the site root under its own name (the page's scripts, the modules), or as
NAME=PATH under NAME (a build output under its site name, as es2shell.js=.../es2shell_web.js). The
self-test page (SELFTEST_FILES, and SELFTEST_OPTIONAL's suite replays) is taken only with --selftest, for a
local build that is never deployed. The report lists what is absent of the site layout (MODULE_ROLES).

Prefetch lists (section 8.4): every --part-log is a part log of web/node/host.mjs or
web/node/game_host.mjs ({"mission", "parts"}, the mission key or "shell" for a shell process);
the logs of one key give prefetch/<key>.json, {"parts": [...]}, the union
of their parts in first-seen order with the logs taken in path order. Every part must be one of
the manifest's.

The build is deterministic: equal inputs give an equal tree hash (SHA-256 over the sorted lines
"<path>\\t<size>\\t<sha256>\\n" of every site file). It fails when a file exceeds MAX_FILE_BYTES or
the site reaches MAX_SITE_BYTES.

usage: build_site.py --cd <extracted CD> --out <site dir> --zstd-pack <exe>
                     [--media <dir>] [--cd-audio <dir>] [--file [<name>=]<path>]... [--selftest]
                     [--part-log <json>]... [--report <json>] [--jobs 4] [--clean]
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)
ZSTD_LEVEL = 19
MAX_FILE_BYTES = 100_000_000
MAX_SITE_BYTES = 1_000_000_000

EXCLUDED_DIRS = {"DIRECTX", "DRIVERS", "INDEO", "WIN32S", "WING", "VER31", "VER95", "DEMOS"}
EXCLUDED_EXTENSIONS = {".EXE", ".DLL", ".DRV", ".VXD", ".HLP", ".CNT"}
MEDIA_EXTENSION = ".AVI"
INSTALL_DIRS = ("DATA", "SAV", "TAPES", "VOL")
INSTALL_ROOT = "C:\\ES2"
CD_ROOT = "D:"
# The language byte of LANGUAGE.CFG: the folder and the extension of ERROR.xxx and MISSION.xxx.
LANGUAGES = {"G": ("GERMAN", "GER"), "E": ("ENGLISH", "ENG"), "F": ("FRENCH", "FRE")}
# As BATCH.EXE writes it: the CD root, then the installation root, CRLF each.
DRIVE_CFG = f"{CD_ROOT}\r\n{INSTALL_ROOT}\r\n"

MEDIA_NAME = re.compile(r"[a-z0-9_]+(\.audio)?\.webm")
CD_AUDIO_FILES = tuple(f"track{n:02d}.opus" for n in range(2, 8)) + ("toc.json",)
RESERVED_ROOT = {"manifest.json", "data", "media", "cd", "prefetch"}
SELFTEST_FILES = ("selftest.html", "selftest.mjs", "selftest_run.mjs", "tick_log_hash.mjs")
# Optional with --selftest: the suite replays of tools/web/selftest_suites.py (game files and journals).
SELFTEST_OPTIONAL = ("selftest_suites.json",)

# The site root's files of section 17 by role; a role is present when any of its names was given.
MODULE_ROLES = {
    "page (index.html)": ("index.html",),
    "page module (app.js)": ("app.js",),
    "service worker (sw.js)": ("sw.js",),
    "game worker (worker.js)": ("worker.js",),
    "audio worklet (audio.js)": ("audio.js",),
    "es2sim module": ("es2sim.wasm",),
    "es2shell module": ("es2shell.wasm",),
    "launcher module": ("launch.wasm",),
    "vfs module": ("vfs.wasm",),
    "tools module": ("tools.wasm",),
}
# web/vfs.mjs missionKey.
MISSION_KEY = re.compile(r"world-?[0-9]+-zone-?[0-9]+")
# The list of the page start (prefetch/shell.json): the shell tour's part logs carry this key.
SHELL_KEY = "shell"


class BuildError(Exception):
    pass


def excluded_name(name):
    upper = name.upper()
    return (upper.startswith("AUTORUN") or upper.startswith("SETUP.") or upper == "SIERRA.INF"
            or os.path.splitext(upper)[1] in EXCLUDED_EXTENSIONS
            or re.fullmatch(r".*\.[^.]{2}_", upper) is not None)


def walk(root, relative=()):
    """(relative parts, path) of every file below root, and the directories, sorted by upper-case name."""
    files, dirs = [], []
    for entry in sorted(os.scandir(root), key=lambda e: e.name.upper()):
        parts = relative + (entry.name,)
        if entry.is_dir(follow_symlinks=False):
            dirs.append(parts)
            sub_files, sub_dirs = walk(entry.path, parts)
            files += sub_files
            dirs += sub_dirs
        elif entry.is_file(follow_symlinks=False):
            files.append((parts, Path(entry.path)))
        else:
            raise BuildError(f"neither file nor directory: {entry.path}")
    return files, dirs


def cd_tree(cd):
    """The D:\\ tree: (included files, included directories, excluded relative paths).

    An excluded directory is listed once and not entered."""
    files, dirs, excluded = [], [], []

    def visit(path, relative):
        for entry in sorted(os.scandir(path), key=lambda e: e.name.upper()):
            parts = relative + (entry.name,)
            is_dir = entry.is_dir(follow_symlinks=False)
            if (is_dir and not relative and entry.name.upper() in EXCLUDED_DIRS) or excluded_name(entry.name):
                excluded.append("/".join(parts))
            elif is_dir:
                dirs.append(parts)
                visit(entry.path, parts)
            elif entry.is_file(follow_symlinks=False):
                files.append((parts, Path(entry.path)))
            else:
                raise BuildError(f"neither file nor directory: {entry.path}")

    visit(cd, ())
    return files, dirs, excluded


def u8(data, offset):
    if offset < 0 or offset + 1 > len(data):
        raise BuildError(f"u8 at {offset:#x} beyond {len(data):#x}")
    return data[offset]


def u16(data, offset):
    if offset < 0 or offset + 2 > len(data):
        raise BuildError(f"u16 at {offset:#x} beyond {len(data):#x}")
    return struct.unpack_from("<H", data, offset)[0]


def u32(data, offset):
    if offset < 0 or offset + 4 > len(data):
        raise BuildError(f"u32 at {offset:#x} beyond {len(data):#x}")
    return struct.unpack_from("<I", data, offset)[0]


def vol_payloads(data):
    """The member payload ranges (offset, length) of a VOLN archive, sorted, without duplicates.

    docs/formats/vol.md: D = u16 at 0x0a, N = u16 at 12 + D, the member table at 18 + D with
    18-byte records whose u32 at +14 is the block offset B; the block holds a 9-byte prefix
    with the payload length L at B + 1, the payload at B + 9 and one further byte, equal to the
    payload's last. That suffix check of es2_vol_open is checked here: the running game opens a
    VOL from its tables alone (es2_vol_open_tables, section 8.3), without reading a payload."""
    directory_bytes = u16(data, 10)
    count = u16(data, 12 + directory_bytes)
    table = 18 + directory_bytes
    if table + 18 * count > len(data):
        raise BuildError("member table beyond the end")
    ranges = set()
    for index in range(count):
        block = u32(data, table + 18 * index + 14)
        length = u32(data, block + 1)
        if block + 9 + length > len(data):
            raise BuildError(f"member {index}: payload {block + 9:#x}+{length:#x} beyond the end")
        if length and (block + 9 + length >= len(data) or data[block + 8 + length] != data[block + 9 + length]):
            raise BuildError(f"member {index}: no suffix byte equal to the payload's last at {block + 9 + length:#x}")
        if length:
            ranges.add((block + 9, length))
    ordered = sorted(ranges)
    for (a, n), (b, _) in zip(ordered, ordered[1:]):
        if a + n > b:
            raise BuildError(f"payloads overlap at {b:#x}")
    return ordered


def file_parts(data):
    """(flags, [ranges per part]) of one file's bytes."""
    if len(data) >= 4 and data[:4] == b"VOLN":
        payloads = vol_payloads(data)
        rest, at = [], 0
        for offset, length in payloads:
            if offset > at:
                rest.append((at, offset - at))
            at = offset + length
        if at < len(data):
            rest.append((at, len(data) - at))
        return ["vol"], [[r] for r in payloads] + ([rest] if rest else [])
    return [], [[(0, len(data))]] if data else []


def win_path(root, parts):
    return root + "\\" + "\\".join(parts) if parts else root + "\\"


def plan_trees(cd):
    """The manifest's files and directories and the parts to write: {sha256: (bytes source)}."""
    files, dirs, excluded = cd_tree(cd)
    entries = []
    directories = [CD_ROOT + "\\"] + [win_path(CD_ROOT, d) for d in dirs]
    sources = []
    for parts, path in files:
        sources.append((win_path(CD_ROOT, parts), path))
    directories += [INSTALL_ROOT[:2] + "\\", INSTALL_ROOT]
    for name in INSTALL_DIRS:
        top = cd / name
        if not top.is_dir():
            raise BuildError(f"no {name} on the CD")
        sub_files, sub_dirs = walk(top, (name,))
        directories.append(win_path(INSTALL_ROOT, (name,)))
        directories += [win_path(INSTALL_ROOT, d) for d in sub_dirs]
        for parts, path in sub_files:
            if excluded_name(parts[-1]):
                raise BuildError(f"an executable in the installation tree: {path}")
            sources.append((win_path(INSTALL_ROOT, parts), path))

    unique = {}
    for site_path, path in sources:
        size = path.stat().st_size
        if path.suffix.upper() == MEDIA_EXTENSION:
            entries.append({"path": site_path, "size": size, "flags": ["media", "no-bytes"], "parts": []})
            continue
        data = path.read_bytes()
        flags, ranges = file_parts(data)
        part_list = []
        for part_ranges in ranges:
            content = b"".join(data[o:o + n] for o, n in part_ranges)
            sha = hashlib.sha256(content).hexdigest()
            unique.setdefault(sha, (path, part_ranges, len(content)))
            part = {"sha256": sha, "length": len(content)}
            if len(part_ranges) == 1:
                part["offset"] = part_ranges[0][0]
            else:
                part["ranges"] = [list(r) for r in part_ranges]
            part_list.append(part)
        entries.append({"path": site_path, "size": size, "flags": flags, "parts": part_list})
    entries.sort(key=lambda e: e["path"].upper())
    directories = sorted(set(directories), key=str.upper)
    return entries, directories, unique, excluded


def languages(entries):
    """manifest.json's "languages": per language byte, what the landing page writes (section 13)."""
    by_key = {e["path"].upper(): e for e in entries}
    result = {}
    for code, (folder, extension) in sorted(LANGUAGES.items()):
        prefix = f"{CD_ROOT}\\{folder}\\"
        sources = [e["path"] for e in entries if e["path"].upper().startswith(prefix)]
        if not sources:
            raise BuildError(f"no language folder {folder} on the CD")
        directories = {f"{INSTALL_ROOT}\\{folder}"}
        copies = []
        for source in sources:
            relative = source[len(prefix):]
            target = f"{INSTALL_ROOT}\\{folder}\\{relative}"
            copies.append([target, source])
            parent = target.rsplit("\\", 1)[0]
            while parent.upper() != f"{INSTALL_ROOT}\\{folder}".upper():
                directories.add(parent)
                parent = parent.rsplit("\\", 1)[0]
        for target, name in (("ERROR.STR", "ERROR"), ("DATA\\MISSION.STR", "MISSION")):
            source = f"{prefix}{name}.{extension}"
            if source.upper() not in by_key:
                raise BuildError(f"no {source} on the CD")
            copies.append([f"{INSTALL_ROOT}\\{target}", source])
        result[code] = {
            "folder": folder,
            "directories": sorted(directories, key=lambda d: (d.count("\\"), d.upper())),
            "copies": copies,
            "writes": {f"{INSTALL_ROOT}\\DATA\\LANGUAGE.CFG": code, f"{INSTALL_ROOT}\\DATA\\DRIVE.CFG": DRIVE_CFG},
        }
    return result


def compress_parts(unique, data_dir, zstd_pack, jobs, work):
    """Writes data/<sha256>.zst for every part; returns {sha256: compressed length}."""
    lines = []
    for index, (sha, (path, ranges, _)) in enumerate(sorted(unique.items())):
        target = data_dir / f"{sha}.zst"
        if len(ranges) == 1:
            offset, length = ranges[0]
            source = path
        else:
            source = work / f"{index}.bin"
            with open(path, "rb") as handle:
                data = handle.read()
            source.write_bytes(b"".join(data[o:o + n] for o, n in ranges))
            offset, length = 0, sum(n for _, n in ranges)
        lines.append(f"{offset}\t{length}\t{os.path.abspath(source)}\t{os.path.abspath(target)}\n")
    jobs = max(1, min(jobs, len(lines)))
    processes = []
    for k in range(jobs):
        job_file = work / f"jobs{k}.txt"
        job_file.write_bytes("".join(lines[k::jobs]).encode("utf-8"))
        processes.append(subprocess.Popen([str(zstd_pack), "-c", str(ZSTD_LEVEL), str(job_file)],
                                          stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, creationflags=NO_WINDOW))
    errors = []
    for process in processes:
        _, err = process.communicate()
        if process.returncode != 0:
            errors.append(err.decode("utf-8", "replace").strip())
    if errors:
        raise BuildError("zstd_pack failed: " + " ".join(errors))
    return {sha: (data_dir / f"{sha}.zst").stat().st_size for sha in unique}


def copy_media(media, out):
    if not (media / "manifest.json").is_file():
        raise BuildError(f"no manifest.json in {media}")
    copied, skipped = [], []
    for path in sorted(media.rglob("*"), key=lambda p: p.as_posix().upper()):
        if not path.is_file():
            continue
        relative = path.relative_to(media)
        if relative.as_posix() == "manifest.json" or (MEDIA_NAME.fullmatch(path.name) and len(relative.parts) <= 2):
            target = out / "media" / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
            copied.append(relative.as_posix())
        else:
            skipped.append(relative.as_posix())
    return copied, skipped


def copy_cd_audio(audio, out):
    target = out / "cd"
    target.mkdir()
    for name in CD_AUDIO_FILES:
        if not (audio / name).is_file():
            raise BuildError(f"no {name} in {audio}")
        shutil.copyfile(audio / name, target / name)
    return list(CD_AUDIO_FILES)


def prefetch_lists(logs, known):
    """{mission: [sha256, ...]} from the part logs (section 8.4)."""
    lists = {}
    for path in sorted((Path(p) for p in logs), key=lambda p: p.as_posix()):
        try:
            log = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as e:
            raise BuildError(f"part log {path}: {e}")
        mission, parts = log.get("mission"), log.get("parts")
        if not isinstance(mission, str) or not (mission == SHELL_KEY or MISSION_KEY.fullmatch(mission)):
            raise BuildError(f"part log {path}: no mission key ({mission!r})")
        if not isinstance(parts, list):
            raise BuildError(f"part log {path}: no part list")
        target = lists.setdefault(mission, {})
        for sha in parts:
            if sha not in known:
                raise BuildError(f"part log {path}: part {sha} is not in the manifest")
            target.setdefault(sha, None)
    return {mission: list(parts) for mission, parts in sorted(lists.items())}


def write_prefetch(lists, out):
    if not lists:
        return
    (out / "prefetch").mkdir()
    for mission, parts in lists.items():
        (out / "prefetch" / f"{mission}.json").write_bytes(
            (json.dumps({"parts": parts}, separators=(",", ":")) + "\n").encode("utf-8"))


def tree_hash(out):
    lines, total, largest, count = [], 0, ("", -1), 0
    for path in sorted(out.rglob("*"), key=lambda p: p.relative_to(out).as_posix()):
        if not path.is_file():
            continue
        relative = path.relative_to(out).as_posix()
        data = path.read_bytes()
        lines.append(f"{relative}\t{len(data)}\t{hashlib.sha256(data).hexdigest()}\n")
        total += len(data)
        count += 1
        if len(data) > largest[1]:
            largest = (relative, len(data))
    return hashlib.sha256("".join(lines).encode("utf-8")).hexdigest(), total, largest, count


def prepare_out(out, clean):
    if out.exists():
        if any(out.iterdir()):
            if not clean:
                raise BuildError(f"{out} is not empty (--clean replaces an earlier site)")
            if not (out / "manifest.json").is_file():
                raise BuildError(f"{out} holds no earlier site (no manifest.json); not deleting it")
            shutil.rmtree(out)
    out.mkdir(parents=True, exist_ok=True)


def site_file(arg):
    """(site name, source path) of one --file: NAME=PATH, or PATH under its own name."""
    name, sep, source = str(arg).partition("=")
    if sep and name and not re.search(r"[\\/:]", name):
        return name, source
    return Path(arg).name, str(arg)


def build(cd, out, zstd_pack, media=None, cd_audio=None, files=(), jobs=4, clean=False, selftest=False,
          part_logs=()):
    cd, out = Path(cd), Path(out)
    if not (cd / "VOL").is_dir():
        raise BuildError(f"{cd} is no extracted CD (no VOL)")
    files = [site_file(f) for f in files]
    names = [name for name, _ in files]
    for name in names:
        if name in RESERVED_ROOT or names.count(name) > 1:
            raise BuildError(f"--file {name}: reserved or given twice")
        if (name in SELFTEST_FILES or name in SELFTEST_OPTIONAL) and not selftest:
            raise BuildError(f"--file {name}: the self-test page needs --selftest")
    if selftest and not all(name in names for name in SELFTEST_FILES):
        raise BuildError("--selftest without " + " and ".join(SELFTEST_FILES))
    entries, directories, unique, excluded = plan_trees(cd)
    langs = languages(entries)
    prefetch = prefetch_lists(part_logs, unique)

    prepare_out(out, clean)
    data_dir = out / "data"
    data_dir.mkdir()
    with tempfile.TemporaryDirectory(prefix="build_site_") as work:
        compressed = compress_parts(unique, data_dir, Path(zstd_pack), jobs, Path(work))
    for entry in entries:
        for part in entry["parts"]:
            part["compressed"] = compressed[part["sha256"]]
    manifest = {
        "format": 1,
        "zstd_level": ZSTD_LEVEL,
        "directories": directories,
        "files": entries,
        "languages": langs,
    }
    (out / "manifest.json").write_bytes((json.dumps(manifest, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8"))

    write_prefetch(prefetch, out)
    media_copied, media_skipped = copy_media(Path(media), out) if media else ([], [])
    audio_copied = copy_cd_audio(Path(cd_audio), out) if cd_audio else []
    for name, source in files:
        shutil.copyfile(source, out / name)

    digest, total, largest, count = tree_hash(out)
    absent = [role for role, options in MODULE_ROLES.items() if not any(o in names for o in options)]
    if not prefetch:
        absent.append("prefetch lists (--part-log)")
    if not media:
        absent.append("media (--media)")
    if not cd_audio:
        absent.append("CD music (--cd-audio)")
    references = sum(len(e["parts"]) for e in entries)
    report = {
        "tree_sha256": digest,
        "site_bytes": total,
        "site_files": count,
        "largest": {"path": largest[0], "bytes": largest[1]},
        "parts": len(unique),
        "part_references": references,
        "parts_raw_bytes": sum(v[2] for v in unique.values()),
        "parts_compressed_bytes": sum(compressed.values()),
        "manifest_files": len(entries),
        "manifest_directories": len(directories),
        "media_entries": sum(1 for e in entries if "media" in e["flags"]),
        "vol_files": sum(1 for e in entries if "vol" in e["flags"]),
        "excluded": excluded,
        "media": media_copied,
        "media_skipped": media_skipped,
        "cd_audio": audio_copied,
        "prefetch": {mission: len(parts) for mission, parts in prefetch.items()},
        "files": sorted(names),
        "selftest": selftest,
        "absent": absent,
        "zstd_level": ZSTD_LEVEL,
        "limits": {"max_file_bytes": MAX_FILE_BYTES, "max_site_bytes": MAX_SITE_BYTES},
    }
    if largest[1] > MAX_FILE_BYTES:
        raise BuildError(f"{largest[0]} has {largest[1]} bytes, over {MAX_FILE_BYTES}")
    if total >= MAX_SITE_BYTES:
        raise BuildError(f"the site has {total} bytes, not below {MAX_SITE_BYTES}")
    return report


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--cd", required=True, help="the extracted CD")
    ap.add_argument("--out", required=True, help="the site directory (new, empty, or an earlier site with --clean)")
    ap.add_argument("--zstd-pack", required=True, help="//tools/web:zstd_pack")
    ap.add_argument("--media", help="encode_media.py's output (manifest.json, *.webm)")
    ap.add_argument("--cd-audio", help="cd_tracks.py's output (track02..07.opus, toc.json)")
    ap.add_argument("--file", action="append", default=[], help="a file for the site root (page script, module); NAME=PATH puts it under NAME")
    ap.add_argument("--selftest", action="store_true", help="take the self-test page (local builds only)")
    ap.add_argument("--part-log", action="append", default=[],
                    help="a part log of web/node/host.mjs (--part-log) for the prefetch lists")
    ap.add_argument("--report", help="write the build report (JSON) here; it is not part of the site")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--clean", action="store_true", help="replace an earlier site in --out")
    args = ap.parse_args(argv)
    try:
        report = build(args.cd, args.out, args.zstd_pack, args.media, args.cd_audio, args.file,
                       args.jobs, args.clean, args.selftest, args.part_log)
    except BuildError as e:
        print(f"build_site: {e}", file=sys.stderr)
        return 1
    if args.report:
        Path(args.report).write_bytes((json.dumps(report, indent=1) + "\n").encode("utf-8"))
    print(f"tree sha256 {report['tree_sha256']}")
    print(f"site {report['site_bytes']} bytes in {report['site_files']} files; "
          f"{report['parts']} parts ({report['part_references']} references), "
          f"{report['parts_raw_bytes']} bytes raw, {report['parts_compressed_bytes']} compressed")
    print(f"largest {report['largest']['path']} ({report['largest']['bytes']} bytes)")
    print(f"manifest {report['manifest_files']} files ({report['media_entries']} media without bytes, "
          f"{report['vol_files']} VOLs), {report['manifest_directories']} directories; "
          f"{len(report['excluded'])} CD paths excluded")
    if report["prefetch"]:
        print("prefetch lists: " + ", ".join(f"{m} ({n} parts)" for m, n in report["prefetch"].items()))
    print("absent: " + (", ".join(report["absent"]) or "nothing"))
    if report["selftest"]:
        print("self-test page included: a local build, not for deployment")
    return 0


if __name__ == "__main__":
    sys.exit(main())
