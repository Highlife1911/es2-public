"""encode_media.py: the decision logic on synthetic numbers, and (with FFmpeg) the encode, the
measurement and a whole conversion of a tiny synthetic clip.

usage: test_encode_media.py <encode_media.py> [<ffmpeg.exe> [<avi_frames.exe>]]

The logic part needs nothing. The FFmpeg part needs libsvtav1, libopus and libdav1d and skips
without them; the whole conversion also needs avi_frames.exe (Windows) and skips without it.
"""
import importlib.util
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

SCRIPT = Path(sys.argv[1]).resolve()
FFMPEG = Path(sys.argv[2]) if len(sys.argv) > 2 else None
AVI_FRAMES = Path(sys.argv[3]) if len(sys.argv) > 3 else None
spec = importlib.util.spec_from_file_location("encode_media", SCRIPT)
em = importlib.util.module_from_spec(spec)
spec.loader.exec_module(em)
checks = 0


def check(value, detail=""):
    global checks
    checks += 1
    if not value:
        raise AssertionError(detail)


def test_stats():
    psnr = ("n:1 mse_avg:0.50 mse_y:0.4 mse_u:0.6 mse_v:0.6 psnr_avg:51.1 psnr_y:52 psnr_u:50 psnr_v:50\n"
            "n:2 mse_avg:0.00 mse_y:0 mse_u:0 mse_v:0 psnr_avg:inf psnr_y:inf psnr_u:inf psnr_v:inf\n"
            "n:3 mse_avg:1.5e+00 mse_y:1 mse_u:2 mse_v:2 psnr_avg:46 psnr_y:46 psnr_u:46 psnr_v:46\n")
    check(em.parse_psnr_stats(psnr) == [0.5, 0.0, 1.5], em.parse_psnr_stats(psnr))
    ssim = "n:1 Y:0.99 U:0.99 V:0.99 All:0.991234 (20.5)\nn:2 Y:1 U:1 V:1 All:1.000000 (inf)\n"
    check(em.parse_ssim_stats(ssim) == [0.991234, 1.0], em.parse_ssim_stats(ssim))
    check(em.mse_to_psnr(0) == math.inf and abs(em.mse_to_psnr(1.0) - 48.1308) < 1e-3, "mse_to_psnr")
    m = em.summarize([0.5, 0.0, 1.5], [0.99, 1.0, 0.98])
    # the mean PSNR is that of the mean squared error, not the mean of the PSNRs
    check(abs(m["mean_psnr"] - em.mse_to_psnr(2.0 / 3)) < 1e-3, m)
    check(m["frames_compared"] == 3 and m["worst_frame"] == 2 and abs(m["min_psnr"] - em.mse_to_psnr(1.5)) < 1e-3, m)
    check(abs(m["mean_ssim"] - 0.99) < 1e-5 and m["min_ssim"] == 0.98, m)
    check(em.summarize([], []) is None and em.summarize([1.0], [0.9, 0.9]) is None and em.summarize([float("nan")], [1.0]) is None, "bad stats")
    check(em.summarize([0.0, 0.0], [1.0, 1.0])["min_psnr"] == 100.0, "identical frames")


def test_thresholds():
    ok = {"mean_psnr": 42.0, "min_psnr": 38.0, "mean_ssim": 0.98}
    check(em.metrics_pass(ok), "exactly at the thresholds")
    for key, low in (("mean_psnr", 41.99), ("min_psnr", 37.99), ("mean_ssim", 0.9799)):
        check(not em.metrics_pass(dict(ok, **{key: low})), key)
    check(not em.metrics_pass(None), "no metrics")


def test_search():
    def monotone(limit):
        calls = []
        def probe(crf):
            calls.append(crf)
            return crf <= limit, {"crf": crf}
        return probe, calls
    for limit in (1, 2, 5, 6, 7, 11, 23, 39, 40):
        probe, calls = monotone(limit)
        chosen, trials = em.search_crf(probe)
        check(chosen == limit, (limit, chosen, calls))
        check(len(calls) == len(set(calls)) and len(calls) <= 9, calls)
        check(list(trials) == calls, "trials in the order measured")
    probe, calls = monotone(0)
    chosen, trials = em.search_crf(probe)
    check(chosen == em.CRF_MIN and not trials[chosen]["crf"] != chosen and calls[-1] == em.CRF_MIN, (chosen, calls))
    probe, calls = monotone(100)
    check(em.search_crf(probe)[0] == em.CRF_MAX, "everything passes")
    # a single CRF in the range
    probe, calls = monotone(3)
    check(em.search_crf(probe, start=3, lo=3, hi=3)[0] == 3 and calls == [3], calls)


def test_supersample():
    floors = {1: {"mean_psnr": 36.0, "min_psnr": 33.0, "mean_ssim": 0.984}, 2: {"mean_psnr": 41.0, "min_psnr": 37.0, "mean_ssim": 0.992},
              4: {"mean_psnr": 47.0, "min_psnr": 44.0, "mean_ssim": 0.997}}
    seen = []
    def floor(k):
        seen.append(k)
        return floors[k]
    check(em.choose_supersample(floor, 300, 200) == (4, floors) and seen == [1, 2, 4], seen)
    seen.clear()
    good = {1: {"mean_psnr": 52.0, "min_psnr": 48.0, "mean_ssim": 0.998}}
    check(em.choose_supersample(lambda k: seen.append(k) or good[k], 300, 200) == (1, good) and seen == [1], seen)
    # the picture stays within MAX_WIDTH: 1200 wide cannot go to 4x
    seen.clear()
    k, got = em.choose_supersample(floor, 1200, 800)
    check(k == 2 and seen == [1, 2], (k, seen))
    check(not em.floor_sufficient(None) and em.floor_sufficient(floors[4]) and not em.floor_sufficient(floors[2]), "floor_sufficient")
    check(em.upscale_filter(1) == "" and em.upscale_filter(2) == "scale=iw*2:ih*2:flags=neighbor,", em.upscale_filter(2))
    check(em.downscale_filter(1) == "" and "scale=iw/4:ih/4:flags=area" in em.downscale_filter(4), em.downscale_filter(4))


def test_structure():
    ref = {"frames": 150, "rate_num": 15, "rate_den": 1, "audio_seconds": 10.0}
    found = {"frames": 150, "rate": 15.0, "video_seconds": 10.0, "audio_seconds": 10.005}
    check(all(em.structure_checks(ref, found).values()), em.structure_checks(ref, found))
    check(not em.structure_checks(ref, dict(found, frames=149))["frame_count_equal"], "count")
    check(not em.structure_checks(ref, dict(found, rate=14.9))["frame_rate_equal"], "rate")
    check(not em.structure_checks(ref, dict(found, video_seconds=10.1))["video_duration_equal"], "video duration")
    check(not em.structure_checks(ref, dict(found, audio_seconds=10.03))["audio_duration_within_opus_frame"], "audio duration")
    check(not em.structure_checks(ref, dict(found, audio_seconds=None))["audio_duration_within_opus_frame"], "missing audio")
    silent = dict(ref, audio_seconds=None)
    check(em.structure_checks(silent, dict(found, audio_seconds=None))["no_audio_both"], "silent movie")
    check(not em.structure_checks(silent, found)["no_audio_both"], "audio appeared")
    check(em.reduce_rate(30, 2) == (15, 1) and em.reduce_rate(15000, 1001) == (15000, 1001), "rates")


def test_names_and_inventory(tmp):
    check(em.output_name("AVI", "INTR_PT1") == "intr_pt1" and em.output_name("AVF", "INTR_PT1") == "avf/intr_pt1"
          and em.output_name("AVG", "C1") == "avg/c1", "names")
    root = tmp / "cd"
    for directory in ("AVI", "AVF", "AVG"):
        (root / directory).mkdir(parents=True)
    (root / "AVI" / "A.AVI").write_bytes(b"first")
    (root / "AVF" / "A.AVI").write_bytes(b"first")          # identical bytes: one conversion
    (root / "AVG" / "A.AVI").write_bytes(b"other")
    (root / "AVI" / "ALPHA.AVI").write_bytes(b"third")      # not named by VSHELL's table
    entries = em.inventory(str(root), None)
    names = sorted(e["name"] for e in entries)
    check(names == ["a", "alpha", "avg/a"], names)
    first = next(e for e in entries if e["name"] == "a")
    check(first["also"] == ["avf/a"] and first["referenced"], first)
    check(not next(e for e in entries if e["name"] == "alpha")["referenced"], "ALPHA is unreferenced")
    check([e["name"] for e in em.inventory(str(root), {"avg/a"})] == ["avg/a"], "--only by site name")
    check([e["name"] for e in em.inventory(str(root), {"a"})][:1] == ["a"], "--only by name")


def ffmpeg_ready():
    if not FFMPEG or not FFMPEG.is_file():
        print("SKIP FFmpeg parts: no ffmpeg")
        return False
    versions = em.tool_versions(str(FFMPEG))
    if not (versions["has_libsvtav1"] and versions["has_libopus"] and versions["has_libdav1d"]):
        print("SKIP FFmpeg parts: ffmpeg lacks libsvtav1, libopus or libdav1d")
        return False
    return True


def clip(width, height, frames):
    """Smooth, slowly moving colour gradients (no wrap-around edges); the same bytes every run."""
    data = bytearray()
    for n in range(frames):
        for y in range(height):
            for x in range(width):
                data += bytes((x * 2 + n * 2, y * 3 + n, x + y + n * 3))
    return bytes(data)


def test_ffmpeg_pipeline(tmp):
    width, height, frames = 96, 64, 8
    ref = tmp / "ref.rgb"
    ref.write_bytes(clip(width, height, frames))
    meta = {"width": width, "height": height, "rate_num": 10, "rate_den": 1}
    ff = str(FFMPEG)
    floor = em.conversion_floor(ff, ref, meta, 1, tmp)
    check(floor["frames_compared"] == frames and floor["mean_psnr"] > 45, floor)
    floor2 = em.conversion_floor(ff, ref, meta, 2, tmp)
    check(floor2["frames_compared"] == frames and floor2["mean_psnr"] > 45, floor2)
    sizes = {}
    for crf in (4, 30):
        out = tmp / ("c%d.webm" % crf)
        em.encode_video(ff, ref, meta, crf, out, tmp)
        sizes[crf] = out.stat().st_size
        found = em.probe_file(str(Path(ff).with_name("ffprobe.exe")), out)
        check(found["frames"] == frames and abs(found["rate"] - 10) < 0.05, found)
        check(found["video"]["codec_name"] == "av1" and found["video"]["profile"] == "Main" and found["video"]["pix_fmt"] == "yuv420p", found)
        check((found["video"]["color_space"], found["video"]["color_range"]) == ("smpte170m", "tv"), found)
    check(sizes[4] > sizes[30], sizes)
    m_low = em.measure(ff, ref, meta, tmp / "c4.webm", tmp)
    m_high = em.measure(ff, ref, meta, tmp / "c30.webm", tmp)
    check(m_low["frames_compared"] == frames and m_low["mean_psnr"] > m_high["mean_psnr"], (m_low, m_high))
    # determinism: the same command gives the same bytes
    again = tmp / "again.webm"
    em.encode_video(ff, ref, meta, 4, again, tmp)
    check(again.read_bytes() == (tmp / "c4.webm").read_bytes(), "encode is not deterministic")
    # the supersampled path: twice the size, measured back at the source size
    big = tmp / "big.webm"
    em.encode_video(ff, ref, meta, 4, big, tmp, 2)
    found = em.probe_file(str(Path(ff).with_name("ffprobe.exe")), big)
    check((found["video"]["width"], found["video"]["height"]) == (2 * width, 2 * height), found)
    m_big = em.measure(ff, ref, meta, big, tmp, 2)
    check(m_big["frames_compared"] == frames, m_big)


def dib_avi(width, height, frames, audio):
    """An uncompressed 24-bit AVI with 8-bit mono 8000 Hz sound (Video for Windows needs no codec)."""
    def chunk(tag, body):
        return tag + struct.pack("<I", len(body)) + body + (b"\0" if len(body) & 1 else b"")
    stride = (width * 3 + 3) // 4 * 4
    bih = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, 0, 0, 0, 0, 0)
    strh_v = struct.pack("<4s4sIHHIIIIIIIIhhhh", b"vids", b"DIB ", 0, 0, 0, 0, 1, 10, 0, len(frames), 0, 0xffffffff, 0, 0, 0, width, height)
    strh_a = struct.pack("<4s4sIHHIIIIIIIIhhhh", b"auds", b"\0\0\0\0", 0, 0, 0, 0, 1, 8000, 0, sum(len(a) for a in audio), 0, 0xffffffff, 1, 0, 0, 0, 0)
    wave = struct.pack("<HHIIHHH", 1, 1, 8000, 8000, 1, 8, 0)
    movi, index, offset = b"", b"", 4
    for n, rgb in enumerate(frames):
        rows = b"".join(b"".join(bytes((rgb[(y * width + x) * 3 + 2], rgb[(y * width + x) * 3 + 1], rgb[(y * width + x) * 3]))
                                 for x in range(width)).ljust(stride, b"\0") for y in range(height - 1, -1, -1))
        for tag, body in ((b"00db", rows), (b"01wb", audio[n])):
            piece = chunk(tag, body)
            index += struct.pack("<4sIII", tag, 0x10, offset, len(body))
            movi += piece
            offset += len(piece)
    lst = lambda kind, body: chunk(b"LIST", kind + body)
    hdrl = lst(b"hdrl", chunk(b"avih", struct.pack("<IIIIIIIIII4I", 100000, 0, 0, 0x10, len(frames), 0, 2, 0, width, height, 0, 0, 0, 0)) +
               lst(b"strl", chunk(b"strh", strh_v) + chunk(b"strf", bih)) + lst(b"strl", chunk(b"strh", strh_a) + chunk(b"strf", wave)))
    body = b"AVI " + hdrl + lst(b"movi", movi) + chunk(b"idx1", index)
    return b"RIFF" + struct.pack("<I", len(body)) + body


def test_ffmpeg_reference(tmp):
    width, height, count = 32, 16, 4
    pixels = clip(width, height, count)
    frames = [pixels[n * width * height * 3:(n + 1) * width * height * 3] for n in range(count)]
    audio = [bytes((n + i) & 255 for i in range(800)) for n in range(count)]
    source = tmp / "FFREF.AVI"
    source.write_bytes(dib_avi(width, height, frames, audio))
    work = tmp / "ffref"
    work.mkdir()
    ff = str(FFMPEG)
    meta = em.decode_reference_ffmpeg(ff, str(Path(ff).with_name("ffprobe.exe")), source, work)
    check((meta["width"], meta["height"], meta["frames"], meta["stream_frames"], meta["rate_num"], meta["rate_den"]) ==
          (width, height, count, count, 10, 1), meta)
    check(meta["source_compression"] == "[0][0][0][0]" and meta["reference"] == "FFmpeg rawvideo", meta)
    check((work / "ref.rgb").read_bytes() == pixels, "RGB frames pass unchanged")
    check(meta["audio"] == {"format_tag": 1, "channels": 1, "sample_rate": 8000, "bits": 8, "samples": 3200, "bytes": 3200}, meta["audio"])
    check(meta["audio_seconds"] == 0.4 and meta["dropped_frames"] == 0, meta)


def test_whole_conversion(tmp):
    if not AVI_FRAMES or not AVI_FRAMES.is_file():
        print("SKIP whole conversion: no avi_frames.exe")
        return
    width, height, count = 96, 64, 10
    pixels = clip(width, height, count)
    frames = [pixels[n * width * height * 3:(n + 1) * width * height * 3] for n in range(count)]
    audio = [bytes((128 + int(60 * math.sin((n * 800 + i) * 0.05))) & 255 for i in range(800)) for n in range(count)]
    root = tmp / "root"
    (root / "AVI").mkdir(parents=True)
    (root / "AVI" / "TINY.AVI").write_bytes(dib_avi(width, height, frames, audio))
    # the same pictures with other sound, as the intros' language versions
    (root / "AVF").mkdir()
    (root / "AVF" / "TINY.AVI").write_bytes(dib_avi(width, height, frames, [bytes(255 - b for b in a) for a in audio]))
    out = tmp / "out"
    code = em.main(["--cd-root", str(root), "--avi-frames", str(AVI_FRAMES), "--ffmpeg", str(FFMPEG), "--out", str(out),
                    "--work", str(tmp / "work"), "--jobs", "1"])
    check(code == 0, code)
    report = json.loads((out / "reports" / "tiny.json").read_text())
    check(report["pass"] is True and report["thresholds_met"] and all(report["structure"].values()), report)
    check(report["video"]["frames"] == count and report["crf"] >= em.CRF_MIN and report["supersample"] == 1, report)
    check(report["final_metrics"]["mean_psnr"] >= 45 and report["final_metrics"]["min_psnr"] >= 40, report["final_metrics"])
    check(report["file"]["streams"] == 1 and all(report["structure"].values()), report["structure"])
    check((out / "tiny.webm").is_file() and (out / "tiny.audio.webm").is_file() and (out / "reports" / "report.md").is_file(), "outputs")
    check(not (out / "avf" / "tiny.webm").exists() and (out / "avf" / "tiny.audio.webm").is_file(), "shared video")
    shared = json.loads((out / "reports" / "avf_tiny.json").read_text())
    check(shared["pass"] is True and shared["video_of"] == "tiny" and shared["crf"] == report["crf"], shared)
    check(shared["output_bytes"] == (out / "avf" / "tiny.audio.webm").stat().st_size, shared["output_bytes"])
    check((out / "tiny.audio.webm").read_bytes() != (out / "avf" / "tiny.audio.webm").read_bytes(), "own sound")
    manifest = json.loads((out / "manifest.json").read_text())
    check(manifest["tiny"]["frames"] == count and (manifest["tiny"]["video"], manifest["tiny"]["audio"]) == ("tiny.webm", "tiny.audio.webm"), manifest)
    check((manifest["avf/tiny"]["video"], manifest["avf/tiny"]["audio"]) == ("tiny.webm", "avf/tiny.audio.webm"), manifest)
    summary = json.loads((out / "reports" / "report.json").read_text())
    check(summary["totals"]["passed"] == 2 and summary["totals"]["failed"] == [], summary["totals"])
    files = ("tiny.webm", "tiny.audio.webm", "avf/tiny.audio.webm")
    first = [(out / f).read_bytes() for f in files]
    # a second run takes the finished movies from the reports
    check(em.main(["--cd-root", str(root), "--avi-frames", str(AVI_FRAMES), "--ffmpeg", str(FFMPEG), "--out", str(out),
                   "--work", str(tmp / "work"), "--jobs", "1"]) == 0 and [(out / f).read_bytes() for f in files] == first, "cached run")
    # forced again: same bytes; --only of the AVF version also converts the movie whose video it takes over
    check(em.main(["--cd-root", str(root), "--avi-frames", str(AVI_FRAMES), "--ffmpeg", str(FFMPEG), "--out", str(out),
                   "--work", str(tmp / "work"), "--jobs", "1", "--force", "--only", "avf/tiny"]) == 0 and
          [(out / f).read_bytes() for f in files] == first, "deterministic")


def main():
    with tempfile.TemporaryDirectory(dir=os.environ.get("TEST_TMPDIR")) as name:
        tmp = Path(name)
        test_stats()
        test_thresholds()
        test_search()
        test_supersample()
        test_structure()
        test_names_and_inventory(tmp)
        if ffmpeg_ready():
            test_ffmpeg_pipeline(tmp)
            test_ffmpeg_reference(tmp)
            test_whole_conversion(tmp)
    print("test_encode_media: PASS (%d checks)" % checks)


main()
