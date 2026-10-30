"""Converts the movies of the CD to WebM (AV1 and Opus) with a per-movie quality search.

docs/web_build.md section 15. For every AVI under <cd root>\\AVI, \\AVF and \\AVG:

1. media/tools/avi_frames.exe decodes the movie through Video for Windows (RGB24 frames, PCM
   sound, frame rate and count): the reference. The movies of FFMPEG_REFERENCE, which have no
   Video for Windows decompressor, are decoded by FFmpeg instead.
2. FFmpeg encodes AV1 (SVT-AV1, Main profile, 8-bit yuv420p) at the source frame rate and frame
   count. The CRF is searched: the largest CRF whose result, decoded with libdav1d and compared
   with the reference in RGB, meets every threshold.
3. The sound becomes Opus in a WebM of its own; the final files are measured again. A movie whose
   video packets equal an earlier movie's (the intros of AVI, AVF and AVG differ only in sound)
   takes over that movie's video file and gets its own sound file.

Acceptance per movie (THRESHOLDS): frame count equal, frame rate equal (within RATE_TOLERANCE),
video duration equal within DURATION_SLACK, sound duration equal within one Opus frame (20 ms plus
DURATION_SLACK), mean PSNR >= 42 dB, every frame >= 38 dB, mean SSIM >= 0.98. The mean PSNR is the
PSNR of the mean squared error over all frames and planes (FFmpeg's "average"), the stricter of the
two usual means when frames differ; the mean of the per-frame PSNR is reported beside it.

Output: <out>/<name>.webm (video only) and <out>/<name>.audio.webm (AVF and AVG movies under <out>/avf
and <out>/avg), <out>/manifest.json naming each movie's two files,
and per movie <report>/<name>.json plus <report>/report.json and report.md. The reports hold no
frames and no media.

usage: encode_media.py --cd-root <dir> --avi-frames <exe> --ffmpeg <exe> --out <dir>
                       [--work <dir>] [--report <dir>] [--jobs 2] [--only a,b] [--force]
"""
import argparse
import concurrent.futures
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import threading
import time
import wave

THRESHOLDS = {"mean_psnr": 42.0, "min_psnr": 38.0, "mean_ssim": 0.98}
RATE_TOLERANCE = 0.005          # relative; WebM timestamps are milliseconds
DURATION_SLACK = 0.003          # seconds; millisecond rounding of the first and last timestamp
OPUS_FRAME = 0.020

# The encoder. Every setting that changes the bytes is here and goes into the reports.
VIDEO_SETTINGS = {
    "encoder": "libsvtav1",
    "preset": 4,
    "svtav1_params": "tune=1:lp=4",     # tune 1 = PSNR; lp only limits threads (same bytes at lp 1, 3, 4, 8)
    "keyint": 150,
    "profile": "main",
    "pix_fmt": "yuv420p",
    # Indeo codes BT.601 YUV; its RGB is that matrix's, so BT.601 keeps the conversion loss lowest.
    "matrix": "smpte170m",
    "range": "tv",
    "forward_scale": "scale=out_color_matrix=bt601:out_range=tv:flags=accurate_rnd+full_chroma_int+bicubic",
    "inverse_scale": "scale=in_color_matrix=bt601:in_range=tv:out_range=pc:flags=accurate_rnd+full_chroma_int+bicubic",
    "decoder": "libdav1d",
    "bitexact": True,
}
AUDIO_SETTINGS = {"encoder": "libopus", "bitrate_mono": "64k", "bitrate_stereo": "96k", "sample_rate": 48000,
                  "frame_duration_ms": 20, "application": "audio", "vbr": "on"}
CRF_MIN, CRF_MAX, CRF_START = 1, 40, 6      # SVT-AV1's CRF 0 is not a lossless mode
# 4:2:0 halves the chroma resolution; for hard-edged palette art the round trip alone stays under the thresholds.
# Such a movie is encoded at k times its size (pixel replication, so the chroma of the enlarged picture is
# the source's) and measured after an area downscale to the source size. The factor is the smallest one
# whose conversion-only result still leaves FLOOR_NEEDED for the codec's noise.
SUPERSAMPLE = (1, 2, 4)
MAX_WIDTH = 2400
FLOOR_NEEDED = {"mean_psnr": 43.0, "min_psnr": 39.0, "mean_ssim": 0.981}

# Movies on the CD that VSHELL.EXE's movie table never names (its strings hold 86 AVI paths).
UNREFERENCED = {"ALPHA", "BRAVO", "DELTA", "OMICRON", "ES2CREDC", "ES2DROP3", "ESTAB2"}
# Movies whose codec has no Video for Windows decompressor on the reference machine (ES2DROP3 is Indeo 4, IV41):
# FFmpeg's decoder makes the reference. Its YUV -> RGB is the one that comes closest to Video for Windows' Indeo 3
# decode (DEATH: 42.5 dB; full range 26.6 dB, bicubic chroma 41.5 dB). Not in the settings stamp: --force re-runs them.
FFMPEG_REFERENCE = {"ES2DROP3"}
FFMPEG_REFERENCE_SCALE = "scale=in_color_matrix=bt601:in_range=tv:flags=accurate_rnd+full_chroma_int+bilinear"

BELOW_NORMAL = getattr(subprocess, "BELOW_NORMAL_PRIORITY_CLASS", 0)
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)
print_lock = threading.Lock()


def log(text):
    with print_lock:
        print(text, flush=True)


# ---- pure logic (tested without FFmpeg) ------------------------------------------------------

def mse_to_psnr(mse):
    return math.inf if mse <= 0 else 10 * math.log10(255.0 * 255.0 / mse)


def parse_psnr_stats(text):
    """Per-frame mean squared error (all planes) from FFmpeg's psnr stats file."""
    values = []
    for line in text.splitlines():
        found = re.search(r"\bmse_avg:([0-9.eE+\-]+|inf|nan)", line)
        if found:
            values.append(float(found.group(1)))
    return values


def parse_ssim_stats(text):
    values = []
    for line in text.splitlines():
        found = re.search(r"\bAll:([0-9.eE+\-]+|inf|nan)", line)
        if found:
            values.append(float(found.group(1)))
    return values


def summarize(mses, ssims):
    """The numbers the thresholds are applied to."""
    if not mses or len(mses) != len(ssims) or any(math.isnan(v) for v in mses + ssims):
        return None
    frame_psnr = [mse_to_psnr(m) for m in mses]
    cap = 100.0
    return {
        "frames_compared": len(mses),
        "mean_psnr": round(mse_to_psnr(sum(mses) / len(mses)), 3),
        "mean_of_frame_psnr": round(sum(min(p, cap) for p in frame_psnr) / len(frame_psnr), 3),
        "min_psnr": round(min(frame_psnr), 3) if min(frame_psnr) != math.inf else 100.0,
        "worst_frame": mses.index(max(mses)),
        "mean_ssim": round(sum(ssims) / len(ssims), 5),
        "min_ssim": round(min(ssims), 5),
    }


def metrics_pass(metrics):
    return bool(metrics and metrics["mean_psnr"] >= THRESHOLDS["mean_psnr"] and
                metrics["min_psnr"] >= THRESHOLDS["min_psnr"] and metrics["mean_ssim"] >= THRESHOLDS["mean_ssim"])


def search_crf(probe, start=CRF_START, lo=CRF_MIN, hi=CRF_MAX):
    """The largest CRF in [lo, hi] for which probe(crf) -> (passed, info) passes, assuming that quality
    falls as the CRF rises: from `start`, step down (or up) with doubling steps, then bisect. Returns
    (chosen, trials); chosen is the best CRF measured to pass, or `lo` when none passes.
    trials maps CRF -> info in the order measured."""
    trials = {}

    def passes(crf):
        if crf not in trials:
            trials[crf] = probe(crf)
        return trials[crf][0]

    good = bad = None
    crf, step = max(lo, min(hi, start)), 2
    while True:
        if passes(crf):
            good = crf if good is None or crf > good else good
            if bad is not None or crf >= hi:
                break
            crf = min(hi, crf + step)
        else:
            bad = crf if bad is None or crf < bad else bad
            if good is not None or crf <= lo:
                break
            crf = max(lo, crf - step)
        step *= 2
    if good is not None and bad is not None:
        while bad - good > 1:
            middle = (good + bad) // 2
            if passes(middle):
                good = middle
            else:
                bad = middle
    chosen = good if good is not None else lo
    return chosen, {crf: trials[crf][1] for crf in trials}


def reduce_rate(num, den):
    g = math.gcd(num, den) or 1
    return num // g, den // g


def structure_checks(ref, found):
    """ref: avi_frames' view (frames, rate_num, rate_den, audio_seconds or None); found: the file's."""
    ref_rate = ref["rate_num"] / ref["rate_den"]
    checks = {
        "frame_count_equal": found["frames"] == ref["frames"],
        "frame_rate_equal": abs(found["rate"] - ref_rate) <= RATE_TOLERANCE * ref_rate,
        "video_duration_equal": abs(found["video_seconds"] - ref["frames"] / ref_rate) <= DURATION_SLACK + 1e-9,
    }
    if ref["audio_seconds"] is not None:
        checks["audio_duration_within_opus_frame"] = (
            found["audio_seconds"] is not None and abs(found["audio_seconds"] - ref["audio_seconds"]) <= OPUS_FRAME + DURATION_SLACK)
    else:
        checks["no_audio_both"] = found["audio_seconds"] is None
    return checks


def output_name(directory, stem):
    """The movie's site name: AVI at the top, AVF and AVG in a folder of their own."""
    directory = directory.lower()
    return "%s" % stem.lower() if directory == "avi" else "%s/%s" % (directory, stem.lower())


# ---- FFmpeg ----------------------------------------------------------------------------------

def run(command, cwd=None, binary=False):
    result = subprocess.run(command, cwd=cwd, capture_output=True, creationflags=BELOW_NORMAL | NO_WINDOW, stdin=subprocess.DEVNULL)
    if result.returncode != 0:
        raise RuntimeError("%s failed (%d): %s" % (Path(command[0]).name, result.returncode,
                                                  result.stderr.decode("utf-8", "replace")[-600:]))
    return result.stdout if binary else result.stdout.decode("utf-8", "replace")


def tool_versions(ffmpeg):
    banner = run([ffmpeg, "-hide_banner", "-version"]).splitlines()
    encoders = run([ffmpeg, "-hide_banner", "-encoders"])
    decoders = run([ffmpeg, "-hide_banner", "-decoders"])
    return {"ffmpeg": banner[0] if banner else "",
            "libavcodec": next((l.strip() for l in banner if l.startswith("libavcodec")), ""),
            "has_libsvtav1": "libsvtav1" in encoders, "has_libopus": "libopus" in encoders,
            "has_libdav1d": "libdav1d" in decoders}


def upscale_filter(k):
    """Pixel replication by k, so that 4:2:0 chroma of the enlarged picture is the source's chroma."""
    return "" if k == 1 else "scale=iw*%d:ih*%d:flags=neighbor," % (k, k)


def downscale_filter(k):
    return "" if k == 1 else ",scale=iw/%d:ih/%d:flags=area+accurate_rnd,format=rgb24" % (k, k)


def encode_video(ffmpeg, ref, meta, crf, out, cwd, k=1):
    s = VIDEO_SETTINGS
    command = [ffmpeg, "-hide_banner", "-nostdin", "-v", "error", "-y",
               "-f", "rawvideo", "-pix_fmt", "rgb24", "-video_size", "%dx%d" % (meta["width"], meta["height"]),
               "-framerate", "%d/%d" % (meta["rate_num"], meta["rate_den"]), "-i", str(ref),
               "-vf", upscale_filter(k) + s["forward_scale"] + ",format=" + s["pix_fmt"],
               "-colorspace", s["matrix"], "-color_primaries", s["matrix"], "-color_trc", s["matrix"],
               "-color_range", s["range"],
               "-c:v", s["encoder"], "-preset", str(s["preset"]), "-crf", str(crf), "-g", str(s["keyint"]),
               "-svtav1-params", s["svtav1_params"], "-fps_mode", "passthrough",
               "-fflags", "+bitexact", "-flags:v", "+bitexact", "-map_metadata", "-1", "-an", str(out)]
    run(command, cwd=cwd)


def compare(ffmpeg, ref, meta, second_input, chain, cwd):
    """PSNR and SSIM of the picture that `chain` makes from `second_input` against the reference frames (RGB)."""
    graph = ("[0:v]settb=1,setpts=N,format=rgb24,split[r1][r2];"
             "[1:v]settb=1,setpts=N,%s,format=rgb24,split[d1][d2];"
             "[r1][d1]psnr=stats_file=psnr.txt[o1];[r2][d2]ssim=stats_file=ssim.txt[o2]" % chain)
    for name in ("psnr.txt", "ssim.txt"):
        Path(cwd, name).unlink(missing_ok=True)
    run([ffmpeg, "-hide_banner", "-nostdin", "-v", "error", "-y",
         "-f", "rawvideo", "-pix_fmt", "rgb24", "-video_size", "%dx%d" % (meta["width"], meta["height"]),
         "-framerate", "%d/%d" % (meta["rate_num"], meta["rate_den"]), "-i", str(ref)] + second_input +
        ["-filter_complex", graph, "-map", "[o1]", "-f", "null", "-", "-map", "[o2]", "-f", "null", "-"], cwd=cwd)
    return summarize(parse_psnr_stats(Path(cwd, "psnr.txt").read_text()), parse_ssim_stats(Path(cwd, "ssim.txt").read_text()))


def measure(ffmpeg, ref, meta, webm, cwd, k=1):
    """PSNR and SSIM of the libdav1d decode of `webm` against the reference frames, in RGB."""
    s = VIDEO_SETTINGS
    return compare(ffmpeg, ref, meta, ["-c:v", s["decoder"], "-threads", "2", "-i", str(webm)],
                   s["inverse_scale"] + ",format=rgb24" + downscale_filter(k), cwd)


def conversion_floor(ffmpeg, ref, meta, k, cwd):
    """The metrics of RGB -> 4:2:0 YUV -> RGB alone, without any codec: the best an encode can reach."""
    s = VIDEO_SETTINGS
    chain = upscale_filter(k) + s["forward_scale"] + ",format=" + s["pix_fmt"] + "," + s["inverse_scale"] + ",format=rgb24" + downscale_filter(k)
    return compare(ffmpeg, ref, meta, ["-f", "rawvideo", "-pix_fmt", "rgb24", "-video_size", "%dx%d" % (meta["width"], meta["height"]),
                                       "-framerate", "%d/%d" % (meta["rate_num"], meta["rate_den"]), "-i", str(ref)], chain, cwd)


def floor_sufficient(m):
    return bool(m and m["mean_psnr"] >= FLOOR_NEEDED["mean_psnr"] and m["min_psnr"] >= FLOOR_NEEDED["min_psnr"] and
                m["mean_ssim"] >= FLOOR_NEEDED["mean_ssim"])


def choose_supersample(floor, width, height):
    """The smallest factor in SUPERSAMPLE whose conversion floor leaves room for the codec's noise (and whose
    picture stays within MAX_WIDTH); floor(k) -> metrics. Returns (k, {k: metrics})."""
    floors, chosen = {}, 1
    for k in SUPERSAMPLE:
        if k > 1 and width * k > MAX_WIDTH:
            break
        floors[k] = floor(k)
        chosen = k
        if floor_sufficient(floors[k]):
            break
    return chosen, floors


def probe_file(ffprobe, webm):
    """Frame count, rate, video and sound end times of the WebM from its packets."""
    data = json.loads(run([ffprobe, "-v", "error", "-show_entries", "stream=index,codec_type,codec_name,profile,pix_fmt,width,height,"
                           "color_space,color_range,avg_frame_rate,channels,sample_rate:packet=stream_index,pts_time,duration_time",
                           "-of", "json", str(webm)]))
    streams = {s["index"]: s for s in data["streams"]}
    ends, counts = {}, {}
    for p in data.get("packets", []):
        i = p["stream_index"]
        counts[i] = counts.get(i, 0) + 1
        ends[i] = max(ends.get(i, 0.0), float(p["pts_time"]) + float(p.get("duration_time", 0)))
    video = next((i for i, s in streams.items() if s["codec_type"] == "video"), None)
    audio = next((i for i, s in streams.items() if s["codec_type"] == "audio"), None)
    found = {"streams": len(streams), "audio_seconds": ends.get(audio) if audio is not None else None,
             "audio": {k: streams[audio].get(k) for k in ("codec_name", "channels", "sample_rate")} if audio is not None else None}
    if video is not None:
        num, den = (int(v) for v in streams[video]["avg_frame_rate"].split("/"))
        found.update({"frames": counts.get(video, 0), "rate": num / den if den else 0.0, "video_seconds": ends.get(video, 0.0),
                      "video": {k: streams[video].get(k) for k in ("codec_name", "profile", "pix_fmt", "width", "height",
                                                                   "color_space", "color_range")}})
    return found


def decoded_audio_seconds(ffmpeg, webm):
    pcm = run([ffmpeg, "-hide_banner", "-nostdin", "-v", "error", "-i", str(webm), "-map", "0:a", "-f", "s16le",
               "-ar", "48000", "-ac", "1", "-"], binary=True)
    return len(pcm) / 2 / 48000.0


def write_video(ffmpeg, candidate, out, cwd):
    run([ffmpeg, "-hide_banner", "-nostdin", "-v", "error", "-y", "-i", str(candidate), "-map", "0:v", "-c:v", "copy",
         "-fflags", "+bitexact", "-map_metadata", "-1", "-map_chapters", "-1", str(out)], cwd=cwd)


def write_audio(ffmpeg, wav, channels, out, cwd):
    a = AUDIO_SETTINGS
    run([ffmpeg, "-hide_banner", "-nostdin", "-v", "error", "-y", "-i", str(wav), "-map", "0:a", "-c:a", a["encoder"],
         "-b:a", a["bitrate_stereo"] if channels > 1 else a["bitrate_mono"], "-vbr", a["vbr"],
         "-application", a["application"], "-frame_duration", str(a["frame_duration_ms"]),
         "-ar", str(a["sample_rate"]), "-flags:a", "+bitexact", "-fflags", "+bitexact", "-map_metadata", "-1",
         "-map_chapters", "-1", "-f", "webm", str(out)], cwd=cwd)


# ---- one movie -------------------------------------------------------------------------------

def settings_stamp():
    blob = json.dumps({"video": VIDEO_SETTINGS, "audio": AUDIO_SETTINGS, "thresholds": THRESHOLDS,
                       "crf": [CRF_MIN, CRF_MAX, CRF_START],
                       "supersample": [SUPERSAMPLE, MAX_WIDTH, FLOOR_NEEDED], "layout": "video and audio files"}, sort_keys=True)
    return hashlib.sha256(blob.encode()).hexdigest()[:16]


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def decode_reference(avi_frames, source, work):
    prefix = work / "ref"
    result = subprocess.run([str(avi_frames), str(source), str(prefix)], capture_output=True, text=True,
                            creationflags=BELOW_NORMAL | NO_WINDOW, stdin=subprocess.DEVNULL, timeout=3600)
    if result.returncode != 0:
        raise RuntimeError("avi_frames: " + result.stderr.strip())
    meta = json.loads((work / "ref.json").read_text())
    if (work / "ref.rgb").stat().st_size != meta["frames"] * meta["width"] * meta["height"] * 3:
        raise RuntimeError("avi_frames output size does not match its header")
    if meta["frames"] != meta["stream_frames"]:
        raise RuntimeError("avi_frames decoded %d of %d frames" % (meta["frames"], meta["stream_frames"]))
    audio = meta.get("audio")
    meta["audio_seconds"] = audio["samples"] / audio["sample_rate"] if audio else None
    meta["reference"] = "Video for Windows"
    return meta


def decode_reference_ffmpeg(ffmpeg, ffprobe, source, work):
    """The same files and header as decode_reference, from FFmpeg's own decoder."""
    data = json.loads(run([ffprobe, "-v", "error", "-show_entries", "stream=codec_type,codec_name,codec_tag_string,width,height,"
                           "avg_frame_rate,nb_frames", "-of", "json", str(source)]))
    video = next(s for s in data["streams"] if s["codec_type"] == "video")
    num, den = reduce_rate(*(int(v) for v in video["avg_frame_rate"].split("/")))
    meta = {"width": video["width"], "height": video["height"], "stream_frames": int(video["nb_frames"]),
            "rate_num": num, "rate_den": den, "pixel_format": "rgb24", "source_compression": video["codec_tag_string"],
            "source_bits": None, "reference": "FFmpeg " + video["codec_name"]}
    decoded = work / "decoded.rgb"
    run([ffmpeg, "-hide_banner", "-nostdin", "-v", "error", "-y", "-i", str(source), "-map", "0:v", "-fps_mode", "passthrough",
         "-vf", FFMPEG_REFERENCE_SCALE + ",format=rgb24", "-f", "rawvideo", str(decoded)], cwd=work)
    pts = [int(line) for line in run([ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries", "frame=pts",
                                      "-of", "csv=p=0", str(source)]).split()]
    frame_bytes = meta["width"] * meta["height"] * 3
    if decoded.stat().st_size != len(pts) * frame_bytes or not pts or pts[0] != 0 or pts != sorted(set(pts)) or pts[-1] >= meta["stream_frames"]:
        raise RuntimeError("FFmpeg decoded %d bytes for frames %s" % (decoded.stat().st_size, pts[:3]))
    # FFmpeg skips the empty chunks of the index; such a chunk is a dropped frame and the previous picture stays.
    with open(decoded, "rb") as src, open(work / "ref.rgb", "wb") as dst:
        picture, following = None, 0
        for n in range(meta["stream_frames"]):
            if following < len(pts) and pts[following] == n:
                picture = src.read(frame_bytes)
                following += 1
            dst.write(picture)
    decoded.unlink()
    meta["frames"] = meta["stream_frames"]
    meta["dropped_frames"] = meta["stream_frames"] - len(pts)
    meta["audio"] = meta["audio_seconds"] = None
    if any(s["codec_type"] == "audio" for s in data["streams"]):
        wav = work / "ref.wav"
        run([ffmpeg, "-hide_banner", "-nostdin", "-v", "error", "-y", "-i", str(source), "-map", "0:a:0", "-c:a", "copy",
             "-fflags", "+bitexact", "-map_metadata", "-1", str(wav)], cwd=work)
        with wave.open(str(wav), "rb") as w:
            meta["audio"] = {"format_tag": 1, "channels": w.getnchannels(), "sample_rate": w.getframerate(),
                             "bits": w.getsampwidth() * 8, "samples": w.getnframes(),
                             "bytes": w.getnframes() * w.getnchannels() * w.getsampwidth()}
        meta["audio_seconds"] = meta["audio"]["samples"] / meta["audio"]["sample_rate"]
    return meta


def convert_movie(ctx, entry, leader=None):
    """Runs the whole conversion of one source file; returns its report (never raises for a movie).
    With `leader` (the report of the movie whose video packets are the same), its video file is taken over."""
    ffmpeg, ffprobe = ctx["ffmpeg"], ctx["ffprobe"]
    name = entry["name"]
    video_out = ctx["out"] / ((leader["name"] if leader else name) + ".webm")
    audio_out = ctx["out"] / (name + ".audio.webm")
    report_path = ctx["report"] / (name.replace("/", "_") + ".json")
    work = ctx["work"] / name.replace("/", "_")
    report = {"name": name, "source": entry["source"], "referenced_by_game": entry["referenced"], "sha256": entry["sha256"],
              "also": entry["also"], "source_bytes": entry["bytes"], "settings_stamp": ctx["stamp"]}
    started = time.time()
    try:
        if leader and not leader.get("pass"):
            raise RuntimeError("the video of %s did not pass" % leader["name"])
        if not ctx["force"] and video_out.is_file() and report_path.is_file():
            old = json.loads(report_path.read_text())
            if (old.get("settings_stamp") == ctx["stamp"] and old.get("sha256") == entry["sha256"] and old.get("pass") is not None and
                    old.get("video_of") == (leader["name"] if leader else None) and (old.get("audio") is None or audio_out.is_file())):
                log("%-18s cached: crf %s, %s" % (name, old.get("crf"), "pass" if old["pass"] else "FAIL"))
                return old
        shutil.rmtree(work, ignore_errors=True)
        work.mkdir(parents=True)
        audio_out.parent.mkdir(parents=True, exist_ok=True)
        if not leader:
            video_out.unlink(missing_ok=True)
        audio_out.unlink(missing_ok=True)
        if entry["path"].stem.upper() in FFMPEG_REFERENCE:
            meta = decode_reference_ffmpeg(ffmpeg, ffprobe, entry["path"], work)
        else:
            meta = decode_reference(ctx["avi_frames"], entry["path"], work)
        ref = work / "ref.rgb"
        report["reference"] = meta["reference"]
        report["video"] = {k: meta[k] for k in ("width", "height", "frames", "rate_num", "rate_den", "source_compression", "source_bits")}
        report["audio"] = meta.get("audio")
        report["picture_sha256"] = sha256_of(ref)
        wav = work / "ref.wav" if meta.get("audio") else None
        if leader:
            if report["picture_sha256"] != leader["picture_sha256"]:
                raise RuntimeError("same video packets as %s but another decoded picture" % leader["name"])
            k, crf = leader["supersample"], leader["crf"]
            report.update({"video_of": leader["name"], "supersample": k, "crf": crf})
        else:
            k, crf = encode_searched(ctx, name, ref, meta, work, report)
            write_video(ffmpeg, work / ("c%d.webm" % crf), video_out, work)
        report["video_file"] = video_out.relative_to(ctx["out"]).as_posix()
        report["audio_file"] = audio_out.relative_to(ctx["out"]).as_posix() if wav else None
        if wav:
            write_audio(ffmpeg, wav, meta["audio"]["channels"], audio_out, work)
        # The files that ship are measured, not the search's candidate.
        final = measure(ffmpeg, ref, meta, video_out, work, k)
        found = probe_file(ffprobe, video_out)
        checks = {"video_file_holds_one_stream": found["streams"] == 1}
        if wav:
            sound = probe_file(ffprobe, audio_out)
            checks["audio_file_holds_one_stream"] = sound["streams"] == 1 and "frames" not in sound
            found["audio_seconds"], found["audio"] = sound["audio_seconds"], sound["audio"]
            found["audio_decoded_seconds"] = round(decoded_audio_seconds(ffmpeg, audio_out), 4)
        checks.update(structure_checks(meta, found))
        if wav:
            checks["audio_decoded_within_opus_frame"] = abs(found["audio_decoded_seconds"] - meta["audio_seconds"]) <= OPUS_FRAME + DURATION_SLACK
        checks["profile_main_yuv420p_av1"] = (found["video"]["codec_name"] == "av1" and found["video"]["profile"] == "Main" and
                                              found["video"]["pix_fmt"] == "yuv420p")
        checks["picture_size"] = (found["video"]["width"], found["video"]["height"]) == (meta["width"] * k, meta["height"] * k)
        checks["matrix_and_range_signalled"] = (found["video"]["color_space"] == VIDEO_SETTINGS["matrix"] and
                                                found["video"]["color_range"] == VIDEO_SETTINGS["range"])
        # A shared video file counts once, with the movie that encoded it.
        report["output_bytes"] = (0 if leader else video_out.stat().st_size) + (audio_out.stat().st_size if wav else 0)
        report["final_metrics"] = final
        report["thresholds_met"] = metrics_pass(final)
        report["structure"] = checks
        report["file"] = {k: (round(v, 4) if isinstance(v, float) else v) for k, v in found.items()}
        report["reference_seconds"] = {"video": round(meta["frames"] * meta["rate_den"] / meta["rate_num"], 4),
                                       "audio": round(meta["audio_seconds"], 4) if wav else None}
        report["pass"] = bool(report["thresholds_met"] and all(checks.values()))
        log("%-18s crf %2d  %6.0f -> %6.0f KB  PSNR %.1f/min %.1f  SSIM %.4f  %s%s" % (
            name, crf, entry["bytes"] / 1024, report["output_bytes"] / 1024, final["mean_psnr"], final["min_psnr"],
            final["mean_ssim"], "pass" if report["pass"] else "FAIL", "  (video of %s)" % leader["name"] if leader else ""))
    except Exception as error:  # a movie that cannot be decoded or encoded is a failed movie in the report
        # No Video for Windows decompressor on the machine: nothing to measure against, so not a failed movie.
        report["pass"] = None if "no decompressor" in str(error) else False
        if report["pass"] is None:
            report["skipped"] = str(error)
        report["error"] = str(error)
        log("%-18s ERROR %s" % (name, str(error)[:200]))
        if not leader:
            video_out.unlink(missing_ok=True)
        audio_out.unlink(missing_ok=True)
    finally:
        report["seconds"] = round(time.time() - started, 1)
        if not ctx["keep_work"]:
            shutil.rmtree(work, ignore_errors=True)
    report_path.write_text(json.dumps(report, indent=1) + "\n", newline="\n")
    return report


def encode_searched(ctx, name, ref, meta, work, report):
    """The supersampling factor and CRF search; leaves the chosen candidate as work/c<crf>.webm."""
    ffmpeg = ctx["ffmpeg"]
    k, floors = choose_supersample(lambda f: conversion_floor(ffmpeg, ref, meta, f, work), meta["width"], meta["height"])
    report["supersample"] = k
    report["conversion_floor"] = {str(f): floors[f] for f in floors}
    if k > 1:
        log("%-18s conversion floor %.1f dB at 1x: encoding at %dx" % (name, floors[1]["mean_psnr"], k))

    def make_probe(factor):
        def probe(crf):
            candidate = work / ("c%d.webm" % crf)
            t = time.time()
            encode_video(ffmpeg, ref, meta, crf, candidate, work, factor)
            m = measure(ffmpeg, ref, meta, candidate, work, factor)
            info = {"crf": crf, "bytes": candidate.stat().st_size, "seconds": round(time.time() - t, 1), "supersample": factor}
            info.update(m or {})
            ok = metrics_pass(m)
            info["pass"] = ok
            return ok, info
        return probe

    # Past the floor's estimate: when even CRF_MIN misses a threshold, the next factor is tried.
    attempts = []
    while True:
        if ctx["crf"] is not None:
            crf = ctx["crf"]
            trials = {crf: make_probe(k)(crf)[1]}
        else:
            crf, trials = search_crf(make_probe(k))
        attempts.append((k, crf, trials))
        larger = [f for f in SUPERSAMPLE if f > k and meta["width"] * f <= MAX_WIDTH]
        if trials[crf]["pass"] or ctx["crf"] is not None or not larger:
            break
        log("%-18s no CRF in %d..%d passes at %dx" % (name, CRF_MIN, CRF_MAX, k))
        k = larger[0]
        report["supersample"] = k
        for stale in work.glob("c*.webm"):
            stale.unlink()
    report["supersample"] = k
    if not trials[crf]["pass"]:
        log("%-18s no CRF in %d..%d passes" % (name, CRF_MIN, CRF_MAX))
    video_only = work / ("c%d.webm" % crf)
    for other in work.glob("c*.webm"):
        if other != video_only:
            other.unlink()
    report["crf"] = crf
    report["trials"] = [t for _, _, tr in attempts for t in tr.values()]
    return k, crf


# ---- inventory and reports -------------------------------------------------------------------

def video_packets_md5(ffmpeg, path):
    try:
        return run([ffmpeg, "-hide_banner", "-nostdin", "-v", "error", "-i", str(path), "-map", "0:v", "-c", "copy", "-f", "md5", "-"]).strip()
    except RuntimeError:
        return None


def inventory(cd_root, only, ffmpeg=None):
    """One entry per distinct source file; identical files (same SHA-256) are converted once. A file whose video
    packets equal an earlier file's (the language versions of the intros differ only in sound) takes over that
    file's video: `picture_of` names it, and it is selected whenever such a file is."""
    seen, packets, entries = {}, {}, []
    for directory in ("AVI", "AVF", "AVG"):
        for path in sorted(Path(cd_root, directory).glob("*.AVI")):
            site = output_name(directory, path.stem)
            digest = sha256_of(path)
            if digest in seen:
                seen[digest]["also"].append(site)
                seen[digest]["selected"] |= not only or path.stem.lower() in only or site in only
                continue
            video = video_packets_md5(ffmpeg, path) if ffmpeg else None
            entry = {"name": site, "path": path, "source": "%s/%s" % (directory, path.name), "sha256": digest,
                     "bytes": path.stat().st_size, "also": [], "referenced": directory != "AVI" or path.stem.upper() not in UNREFERENCED,
                     "picture_of": packets[video]["name"] if video in packets else None,
                     "selected": not only or path.stem.lower() in only or site in only}
            if video and video not in packets:
                packets[video] = entry
            seen[digest] = entry
            entries.append(entry)
    leaders = {e["picture_of"] for e in entries if e["selected"] and e["picture_of"]}
    return [e for e in entries if e["selected"] or e["name"] in leaders]


def write_reports(reports, ctx, versions, wall):
    reports = sorted(reports, key=lambda r: r["name"])
    failed = [r["name"] for r in reports if r.get("pass") is False]
    skipped = [r["name"] for r in reports if r.get("pass") is None]
    total = {"movies": len(reports), "passed": sum(1 for r in reports if r.get("pass")), "failed": failed, "skipped": skipped,
             "source_bytes": sum(r["source_bytes"] for r in reports), "output_bytes": sum(r.get("output_bytes", 0) for r in reports),
             "wall_seconds": round(wall)}
    summary = {"thresholds": THRESHOLDS, "video_settings": VIDEO_SETTINGS, "audio_settings": AUDIO_SETTINGS,
               "crf_range": [CRF_MIN, CRF_MAX], "tools": versions, "totals": total,
               "movies": [{k: r.get(k) for k in ("name", "source", "sha256", "also", "referenced_by_game", "reference", "video_of", "supersample", "crf", "source_bytes", "output_bytes", "pass", "error")} |
                          {"mean_psnr": (r.get("final_metrics") or {}).get("mean_psnr"), "min_psnr": (r.get("final_metrics") or {}).get("min_psnr"),
                           "mean_ssim": (r.get("final_metrics") or {}).get("mean_ssim"),
                           "structure_failed": [k for k, v in (r.get("structure") or {}).items() if not v]} for r in reports]}
    ctx["report"].joinpath("report.json").write_text(json.dumps(summary, indent=1) + "\n", newline="\n")
    lines = ["# Movie conversion", "",
             "AV1 (%s preset %d, %s, CRF searched in %d..%d), Opus %s mono / %s stereo. Thresholds: mean PSNR >= %g dB, every frame >= %g dB, mean SSIM >= %g."
             % (VIDEO_SETTINGS["encoder"], VIDEO_SETTINGS["preset"], VIDEO_SETTINGS["svtav1_params"], CRF_MIN, CRF_MAX,
                AUDIO_SETTINGS["bitrate_mono"], AUDIO_SETTINGS["bitrate_stereo"], THRESHOLDS["mean_psnr"], THRESHOLDS["min_psnr"], THRESHOLDS["mean_ssim"]),
             "", "%s. %d of %d movies pass; source %.1f MB, output %.1f MB." % (versions["ffmpeg"], total["passed"], total["movies"],
                                                                      total["source_bytes"] / 1e6, total["output_bytes"] / 1e6),
             "", "| movie | size | CRF | source KB | webm KB | mean PSNR | min PSNR | mean SSIM | result |", "|---|---:|---:|---:|---:|---:|---:|---:|---|"]
    for r in reports:
        m = r.get("final_metrics") or {}
        lines.append("| %s%s | %s | %s | %.0f | %.0f | %s | %s | %s | %s |" % (
            r["name"], "" if r.get("referenced_by_game", True) else " (not in VSHELL's table)",
            "%dx" % r["supersample"] if r.get("supersample", 1) > 1 else "1x", r.get("crf", ""), r["source_bytes"] / 1024,
            r.get("output_bytes", 0) / 1024, m.get("mean_psnr", ""), m.get("min_psnr", ""), m.get("mean_ssim", ""),
            "pass" if r.get("pass") else "skipped: no decoder" if r.get("pass") is None else "FAIL " + (r.get("error") or ",".join(k for k, v in (r.get("structure") or {}).items() if not v) or "thresholds")[:80]))
    ctx["report"].joinpath("report.md").write_text("\n".join(lines) + "\n", newline="\n")
    manifest = {r["name"]: {"video": r["video_file"], "audio": r["audio_file"], "source": r["source"], "also": r["also"], "bytes": r.get("output_bytes"),
                            "width": r.get("video", {}).get("width"), "height": r.get("video", {}).get("height"),
                            "frames": r.get("video", {}).get("frames"), "supersample": r.get("supersample", 1)}
                for r in reports if r.get("pass")}
    ctx["out"].joinpath("manifest.json").write_text(json.dumps(manifest, indent=1, sort_keys=True) + "\n", newline="\n")
    return total


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--cd-root", required=True)
    ap.add_argument("--avi-frames", required=True)
    ap.add_argument("--ffmpeg", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--work")
    ap.add_argument("--report")
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--only", help="comma-separated movie names (c1, intr_pt1, avg/intr_pt1)")
    ap.add_argument("--crf", type=int, help="measure one CRF instead of searching")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--keep-work", action="store_true")
    args = ap.parse_args(argv)
    out = Path(args.out).resolve()
    ffmpeg = str(Path(args.ffmpeg).resolve())
    ffprobe = str(Path(ffmpeg).with_name("ffprobe.exe" if ffmpeg.endswith(".exe") else "ffprobe"))
    ctx = {"ffmpeg": ffmpeg, "ffprobe": ffprobe, "avi_frames": Path(args.avi_frames).resolve(), "out": out,
           "work": Path(args.work).resolve() if args.work else out.parent / "work",
           "report": Path(args.report).resolve() if args.report else out / "reports",
           "force": args.force, "keep_work": args.keep_work, "crf": args.crf, "stamp": settings_stamp()}
    versions = tool_versions(ffmpeg)
    if not (versions["has_libsvtav1"] and versions["has_libopus"] and versions["has_libdav1d"]):
        print("FFmpeg lacks libsvtav1, libopus or libdav1d", file=sys.stderr)
        return 2
    for key in ("out", "work", "report"):
        ctx[key].mkdir(parents=True, exist_ok=True)
    entries = inventory(args.cd_root, {s.strip().lower() for s in args.only.split(",")} if args.only else None, ffmpeg)
    # Largest sources first: the long movies start early and the short ones fill the gaps.
    entries.sort(key=lambda e: -e["bytes"])
    log("%d distinct movies, %d jobs, settings %s" % (len(entries), args.jobs, ctx["stamp"]))
    started = time.time()
    # The movies that encode a video first, then those that take over another's.
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        reports = list(pool.map(lambda e: convert_movie(ctx, e), [e for e in entries if not e["picture_of"]]))
        by_name = {r["name"]: r for r in reports}
        reports += list(pool.map(lambda e: convert_movie(ctx, e, by_name[e["picture_of"]]), [e for e in entries if e["picture_of"]]))
    total = write_reports(reports, ctx, versions, time.time() - started)
    log("%d of %d movies pass; %.1f MB -> %.1f MB; %ds" % (total["passed"], total["movies"], total["source_bytes"] / 1e6,
                                                          total["output_bytes"] / 1e6, total["wall_seconds"]))
    return 0 if not total["failed"] else 1


if __name__ == "__main__":
    sys.exit(main())
