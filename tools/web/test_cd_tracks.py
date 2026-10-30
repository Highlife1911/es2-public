# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""cd_tracks.py on synthetic raw CD images with known silences.

usage: test_cd_tracks.py <cd_tracks.py> [<ffmpeg.exe>]

The images hold a few data sectors with the CD-ROM sync pattern, then pseudo-random "music"
(and quiet passages) separated by runs of digital silence of known lengths. The Opus part
needs FFmpeg with libopus and skips without it.
"""
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

SCRIPT = Path(sys.argv[1]).resolve()
FFMPEG = Path(sys.argv[2]) if len(sys.argv) > 2 else None
spec = importlib.util.spec_from_file_location("cd_tracks", SCRIPT)
cd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cd)
checks = 0


def check(value, detail=""):
    global checks
    checks += 1
    if not value:
        raise AssertionError(detail)


class Noise:
    """A small deterministic generator (no dependence on the random module's algorithms)."""

    def __init__(self, seed):
        self.state = seed

    def sector(self, amplitude):
        samples = []
        for _ in range(cd.SECTOR // 2):
            self.state = (self.state * 1103515245 + 12345) & 0x7FFFFFFF
            samples.append((self.state >> 8) % (2 * amplitude + 1) - amplitude)
        return struct.pack("<%dh" % len(samples), *samples)


def data_sector(n):
    return cd.SYNC + bytes((n + i) & 255 for i in range(cd.SECTOR - 12))


def build(path, data_sectors, plan):
    """plan: [(kind, sectors)], kind 'loud', 'quiet' or 'silence'. Returns the start sector of each piece."""
    noise = Noise(7)
    starts, sector = [], data_sectors
    with open(path, "wb") as f:
        for n in range(data_sectors):
            f.write(data_sector(n))
        for kind, count in plan:
            starts.append(sector)
            for _ in range(count):
                f.write(cd.ZERO if kind == "silence" else noise.sector(12000 if kind == "loud" else 2))
            sector += count
    return starts


def open_image(path):
    return cd.Image(path)


def test_split_by_silence(tmp):
    plan = [("loud", 1), ("silence", 80),            # at the start: not a boundary
            ("loud", 300), ("silence", 80),          # A
            ("loud", 400), ("silence", 120),         # B
            ("loud", 200), ("silence", 74),          # shorter than a second: a pause inside a track
            ("loud", 250), ("silence", 100),         # C
            ("loud", 300), ("silence", 90),          # D
            ("loud", 350), ("silence", 76),          # E: the fifth in length
            ("loud", 200), ("silence", 100)]         # at the end: not a boundary
    path = tmp / "silences.img"
    starts = build(path, 9, plan)
    image = open_image(path)
    try:
        total = image.sectors
        check(cd.find_data_end(image) == 9, "data end")
        data, runs, boundaries, details, candidates = cd.split_image(image, 5)
        check(data == 9, data)
        check(all(n >= 1 for _, n in runs) and (starts[1], 80) in runs and (starts[7], 74) in runs, runs)
        ends = lambda i: starts[i] + plan[i][1]
        check(boundaries == sorted([ends(3), ends(5), ends(9), ends(11)]), (boundaries, [ends(i) for i in (3, 5, 9, 11)]))
        check([d["kind"] for d in details] == ["silence"] * 4, details)
        check((starts[1], 80) not in [(s, n) for s, n in candidates], "edge run is not a candidate")
        check((starts[7], 74) not in [(s, n) for s, n in candidates], "short pause is not a candidate")
        tracks = cd.build_toc(total, data, boundaries)
        check([t["number"] for t in tracks] == [1, 2, 3, 4, 5, 6], tracks)
        check(tracks[0] == {"number": 1, "type": "data", "start": 0, "length": 9}, tracks[0])
        check(tracks[1]["start"] == 9 and tracks[-1]["start"] + tracks[-1]["length"] == total, tracks)
        for a, b in zip(tracks[1:], tracks[2:]):
            check(a["start"] + a["length"] == b["start"], (a, b))
        check(sum(t["length"] for t in tracks) == total, "the tracks cover the image")
        report = cd.toc_report("silences.img", total, data, runs, details, candidates, tracks)
        check("| 5 |" in report and "Listening check" in report and "yes" in report, report)
    finally:
        image.close()


def test_five_equal_runs(tmp):
    # Six pieces and five long silences: the four longest are chosen, the shortest left out.
    lengths = [90, 100, 95, 100, 85]
    plan = [("loud", 200)]
    for n in lengths:
        plan += [("silence", n), ("loud", 200)]
    path = tmp / "five.img"
    starts = build(path, 3, plan)
    image = open_image(path)
    try:
        data, runs, boundaries, details, candidates = cd.split_image(image, 5)
        # silence k sits at plan index 1 + 2k; the shortest (85, k = 4) is left out, equal ones keep the earlier first
        expected = [starts[1 + 2 * k] + lengths[k] for k in (0, 1, 2, 3)]
        check(boundaries == expected, (boundaries, expected))
        data, runs, six, details, candidates = cd.split_image(image, 6)
        check(six == expected + [starts[9] + 85], six)
    finally:
        image.close()


def test_tie_break():
    runs = [(1000, 100), (2000, 100), (3000, 100), (4000, 99)]
    boundaries, details, candidates = cd.choose_boundaries(runs, 0, 6000, 2)
    check(boundaries == [1100, 2100], boundaries)


def test_quiet_window_fallback(tmp):
    plan = [("loud", 400), ("quiet", 100), ("loud", 400), ("silence", 90), ("loud", 400), ("quiet", 100),
            ("loud", 400), ("silence", 90), ("loud", 400)]
    path = tmp / "quiet.img"
    starts = build(path, 4, plan)
    image = open_image(path)
    try:
        data, runs, boundaries, details, candidates = cd.split_image(image, 5)
        check(len(boundaries) == 4 and boundaries == sorted(boundaries), boundaries)
        kinds = sorted(d["kind"] for d in details)
        check(kinds == ["quiet-window"] * 2 + ["silence"] * 2, kinds)
        for index in (1, 5):
            inside = [d for d in details if d["kind"] == "quiet-window" and starts[index] <= d["start"] and d["start"] + d["length"] <= starts[index] + 100]
            check(len(inside) == 1, (index, details))
        silences = sorted(d["boundary"] for d in details if d["kind"] == "silence")
        check(silences == [starts[3] + 90, starts[7] + 90], silences)
    finally:
        image.close()


def test_errors(tmp):
    bad = tmp / "bad.img"
    bad.write_bytes(bytes(cd.SECTOR + 5))
    try:
        cd.Image(bad)
        check(False, "image of a wrong size accepted")
    except ValueError:
        check(True)
    only_data = tmp / "data_only.img"
    build(only_data, 5, [])
    image = open_image(only_data)
    try:
        try:
            cd.split_image(image, 5)
            check(False, "an image without audio accepted")
        except ValueError:
            check(True)
    finally:
        image.close()
    no_data = tmp / "no_data.img"
    build(no_data, 0, [("loud", 50)])
    image = open_image(no_data)
    try:
        check(cd.find_data_end(image) == 0, "no sync pattern, no data track")
    finally:
        image.close()
    check(cd.timecode(75 * 61 + 7) == "1:01.07", cd.timecode(75 * 61 + 7))


def test_opus(tmp):
    if not FFMPEG or not FFMPEG.is_file():
        print("SKIP Opus: no ffmpeg")
        return
    found = subprocess.run([str(FFMPEG), "-hide_banner", "-encoders"], capture_output=True, text=True).stdout
    if "libopus" not in found:
        print("SKIP Opus: ffmpeg without libopus")
        return
    plan = [("loud", 1), ("loud", 150), ("silence", 80), ("loud", 160), ("silence", 90), ("loud", 170), ("silence", 100),
            ("loud", 180), ("silence", 110), ("loud", 190), ("silence", 75), ("loud", 5)]
    path = tmp / "opus.img"
    build(path, 6, plan)
    out = tmp / "cd"
    code = cd.main(["--image", str(path), "--ffmpeg", str(FFMPEG), "--out", str(out), "--report", str(tmp / "report")])
    check(code == 0, code)
    toc = json.loads((out / "toc.json").read_text())
    check(toc["frames_per_second"] == 75 and toc["data_sectors"] == 6 and len(toc["tracks"]) == 7, toc)
    check(sum(t["length"] for t in toc["tracks"]) == toc["image_sectors"], "lengths cover the image")
    for t in toc["tracks"][1:]:
        got = cd.decoded_samples(str(FFMPEG), out / t["file"])
        check(got == t["length"] * 640, (t["file"], got, t["length"] * 640))
        probe = subprocess.run([str(FFMPEG), "-hide_banner", "-i", str(out / t["file"])], capture_output=True, text=True).stderr
        check("Audio: opus" in probe and "48000 Hz, stereo" in probe, probe)
    check((tmp / "report" / "toc_report.md").is_file() and (tmp / "report" / "toc_report.json").is_file(), "reports")
    # Deterministic: a second run writes the same bytes
    out2 = tmp / "cd2"
    cd.main(["--image", str(path), "--ffmpeg", str(FFMPEG), "--out", str(out2), "--report", str(tmp / "report2")])
    for t in toc["tracks"][1:]:
        check((out / t["file"]).read_bytes() == (out2 / t["file"]).read_bytes(), "not deterministic: " + t["file"])


def main():
    with tempfile.TemporaryDirectory(dir=os.environ.get("TEST_TMPDIR")) as name:
        tmp = Path(name)
        test_split_by_silence(tmp)
        test_five_equal_runs(tmp)
        test_tie_break()
        test_quiet_window_fallback(tmp)
        test_errors(tmp)
        test_opus(tmp)
    print("test_cd_tracks: PASS (%d checks)" % checks)


main()
