#!/usr/bin/env python3
"""Cuts the CD audio tracks out of the raw CD image and writes cd/toc.json and cd/trackNN.opus.

The image has 2352-byte sectors and no CUE sheet. The data track ends at the last sector that
starts with the CD-ROM sync pattern; the audio after it is split into tracks at the longest
runs of digital silence (docs/web_build.md section 15). Standard library only; FFmpeg encodes.

usage: cd_tracks.py --image IMAGE --ffmpeg FFMPEG --out DIR [--report DIR] [--tracks 6] [--no-encode]
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys

SECTOR = 2352
SYNC = bytes([0x00] + [0xFF] * 10 + [0x00])
RATE = 75                       # frames (sectors) per second
SAMPLES_PER_SECTOR = SECTOR // 4
OPUS_RATE = 48000
OPUS_PER_SECTOR = OPUS_RATE // RATE         # 640 samples per channel; exact
MIN_SILENCE = RATE                          # one second
EDGE_MARGIN = RATE                          # a boundary needs a second of sound on both sides
FIRST_AUDIO_TRACK = 2
BELOW_NORMAL = getattr(subprocess, "BELOW_NORMAL_PRIORITY_CLASS", 0)
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)
OPUS_SETTINGS = {"encoder": "libopus", "bitrate": "160k", "sample_rate": OPUS_RATE, "channels": 2,
                 "application": "audio", "vbr": "on", "frame_duration_ms": 20}
ZERO = bytes(SECTOR)


def timecode(frames):
    """mm:ss.ff of a frame count (ff in 1/75 s)."""
    return "%d:%02d.%02d" % (frames // (RATE * 60), frames // RATE % 60, frames % RATE)


# ---- image reading ---------------------------------------------------------------------------

class Image:
    def __init__(self, path):
        self.path = Path(path)
        size = self.path.stat().st_size
        if size == 0 or size % SECTOR:
            raise ValueError("%s is not a raw image of %d-byte sectors (%d bytes)" % (path, SECTOR, size))
        self.sectors = size // SECTOR
        self.file = open(self.path, "rb")

    def read(self, first, count):
        self.file.seek(first * SECTOR)
        data = self.file.read(count * SECTOR)
        if len(data) != count * SECTOR:
            raise ValueError("short read at sector %d" % first)
        return data

    def close(self):
        self.file.close()


def find_data_end(image):
    """The number of sectors up to and including the last one with the sync pattern (0 when none)."""
    for sector in range(image.sectors - 1, -1, -1):
        if image.read(sector, 1)[:12] == SYNC:
            return sector + 1
    return 0


def silence_runs(image, first, chunk=2048):
    """Runs of all-zero sectors from `first` on: [(start, length)], sector numbers of the image."""
    runs, start = [], None
    sector = first
    while sector < image.sectors:
        count = min(chunk, image.sectors - sector)
        data = image.read(sector, count)
        for i in range(count):
            silent = data[i * SECTOR:(i + 1) * SECTOR] == ZERO
            if silent and start is None:
                start = sector + i
            elif not silent and start is not None:
                runs.append((start, sector + i - start))
                start = None
        sector += count
    if start is not None:
        runs.append((start, image.sectors - start))
    return runs


def sector_energies(image, first):
    """Sum of squared samples per audio sector (for the quiet-window fallback)."""
    energies = []
    sector = first
    while sector < image.sectors:
        count = min(1024, image.sectors - sector)
        data = image.read(sector, count)
        for i in range(count):
            samples = struct.unpack_from("<%dh" % (SECTOR // 2), data, i * SECTOR)
            energies.append(sum(v * v for v in samples))
        sector += count
    return energies


# ---- the split -------------------------------------------------------------------------------

def interior_runs(runs, audio_start, total):
    """Runs of at least a second with a second of sound before and after: the candidates for a boundary."""
    return [(s, n) for s, n in runs
            if n >= MIN_SILENCE and s - audio_start >= EDGE_MARGIN and total - (s + n) >= EDGE_MARGIN]


def quietest_windows(energies, audio_start, count, exclude):
    """The `count` quietest non-overlapping one-second windows (greedy), away from `exclude` sectors'
    windows. Returns [(start, length)] in image sectors, length RATE."""
    n = len(energies)
    if n < RATE:
        return []
    prefix = [0]
    for e in energies:
        prefix.append(prefix[-1] + e)
    windows = sorted(((prefix[i + RATE] - prefix[i], i) for i in range(EDGE_MARGIN, n - RATE - EDGE_MARGIN + 1)))
    chosen = []
    taken = [(s - audio_start, n_) for s, n_ in exclude]
    for _, i in windows:
        if all(i + RATE <= s or i >= s + length for s, length in taken):
            chosen.append((audio_start + i, RATE))
            taken.append((i, RATE))
            if len(chosen) == count:
                break
    return chosen


def choose_boundaries(runs, audio_start, total, count, energies=lambda: []):
    """The `count` track boundaries inside the audio area.

    The longest interior silence runs (ties: the earlier) give the boundaries; a boundary is the
    end of its run, where the next track's sound starts (the run is the pregap, which an MCI
    track length includes). Fewer than `count` runs: the quietest one-second windows fill up
    (the window's middle). `energies` is called only then.
    Returns (boundaries sorted, details sorted by position, candidates sorted by length)."""
    candidates = sorted(interior_runs(runs, audio_start, total), key=lambda r: (-r[1], r[0]))
    picked = [{"kind": "silence", "start": s, "length": n, "boundary": s + n} for s, n in candidates[:count]]
    if len(picked) < count:
        windows = quietest_windows(energies(), audio_start, count - len(picked), candidates[:count])
        picked += [{"kind": "quiet-window", "start": s, "length": n, "boundary": s + n // 2} for s, n in windows]
    picked.sort(key=lambda d: d["boundary"])
    return [d["boundary"] for d in picked], picked, candidates


def build_toc(total, data_sectors, boundaries):
    """Track 1 is the data track; audio tracks start at the end of the data track and at every
    boundary and each runs to the next start (the last one to the end of the image)."""
    starts = [data_sectors] + list(boundaries)
    ends = starts[1:] + [total]
    tracks = [{"number": 1, "type": "data", "start": 0, "length": data_sectors}]
    for i, (start, end) in enumerate(zip(starts, ends)):
        number = FIRST_AUDIO_TRACK + i
        tracks.append({"number": number, "type": "audio", "start": start, "length": end - start,
                       "file": "track%02d.opus" % number})
    return tracks


# ---- Opus ------------------------------------------------------------------------------------

def run(command, stdin_bytes=None):
    result = subprocess.run(command, input=stdin_bytes, capture_output=True, creationflags=BELOW_NORMAL | NO_WINDOW,
                            stdin=None if stdin_bytes is not None else subprocess.DEVNULL)
    if result.returncode != 0:
        raise RuntimeError("%s failed (%d): %s" % (Path(command[0]).name, result.returncode,
                                                  result.stderr.decode("utf-8", "replace")[-600:]))
    return result.stdout


def encode_track(ffmpeg, pcm, sectors, out):
    """Stereo 16-bit 44.1 kHz PCM -> Opus 160 kb/s at 48 kHz whose decoded length is exactly `sectors` * 640 samples."""
    s = OPUS_SETTINGS
    length = sectors * OPUS_PER_SECTOR
    run([ffmpeg, "-hide_banner", "-nostdin", "-v", "error", "-y", "-f", "s16le", "-ar", "44100", "-ac", "2", "-i", "pipe:0",
         "-af", "aresample=%d,apad=whole_len=%d,atrim=end_sample=%d,asetpts=N/SR/TB" % (s["sample_rate"], length, length),
         "-c:a", s["encoder"], "-b:a", s["bitrate"], "-vbr", s["vbr"], "-application", s["application"],
         "-frame_duration", str(s["frame_duration_ms"]), "-ar", str(s["sample_rate"]), "-ac", str(s["channels"]),
         "-fflags", "+bitexact", "-flags:a", "+bitexact", "-map_metadata", "-1", str(out)], stdin_bytes=pcm)


def decoded_samples(ffmpeg, path):
    """Samples per channel the Opus file decodes to at 48 kHz."""
    pcm = run([ffmpeg, "-hide_banner", "-nostdin", "-v", "error", "-i", str(path), "-f", "s16le", "-ar", str(OPUS_RATE),
               "-ac", "2", "-"])
    return len(pcm) // 4


# ---- reports ---------------------------------------------------------------------------------

def toc_report(image_name, total, data_sectors, runs, details, candidates, tracks, decoded=None):
    audio_start = data_sectors
    lines = ["# CD table of contents", "",
             "Image `%s`: %d sectors of %d bytes (%s); the data track is sectors 0..%d (%d sectors, %s); the audio area is %d sectors (%s)."
             % (image_name, total, SECTOR, timecode(total), data_sectors - 1, data_sectors, timecode(data_sectors),
                total - data_sectors, timecode(total - data_sectors)), "",
             "## Digital silence of at least one second in the audio area", "",
             "| # | start sector | start | length (frames) | length | ends at | used as boundary |", "|---:|---:|---|---:|---|---|---|"]
    used = {d["start"] for d in details if d["kind"] == "silence"}
    for i, (start, length) in enumerate(sorted(r for r in runs if r[1] >= MIN_SILENCE)):
        note = "yes" if start in used else ("no: at the edge of the audio area" if (start - audio_start < EDGE_MARGIN or
                                                                                    total - (start + length) < EDGE_MARGIN) else "no: not among the longest runs")
        lines.append("| %d | %d | %s | %d | %s | %s | %s |" % (i + 1, start, timecode(start - audio_start), length, timecode(length),
                                                              timecode(start + length - audio_start), note))
    quiet = [d for d in details if d["kind"] != "silence"]
    if quiet:
        lines += ["", "Fewer long silences than boundaries: the quietest one-second windows fill up, starting at sectors "
                  + ", ".join(str(d["start"]) for d in quiet) + "."]
    lines += ["", "## Tracks", "",
              "A track runs from its start to the next track's start, so it ends with the following silence. Times are from the start of the audio area; the Opus file's own time is from its start.", "",
              "| track | start sector | start (audio area) | length (frames) | length | file |", "|---:|---:|---|---:|---|---|"]
    for t in tracks:
        if t["type"] == "audio":
            lines.append("| %d | %d | %s | %d | %s | `cd/%s` |" % (t["number"], t["start"], timecode(t["start"] - audio_start),
                                                              t["length"], timecode(t["length"]), t["file"]))
    if decoded:
        lines += ["", "Decoded lengths: " + ", ".join("track %02d %d samples (TOC %d)" % (n, got, want) for n, (got, want) in sorted(decoded.items())) + "."]
    lines += ["", "## Listening check (open)", "",
              "The boundaries are a hypothesis. For each boundary, play the end of the earlier track and the start of the later one:", ""]
    audio = [t for t in tracks if t["type"] == "audio"]
    for before, after in zip(audio, audio[1:]):
        n_before = before["length"]
        lines.append("- Boundary %d/%d at sector %d: `cd/%s` from %s to its end (%s); the music of the earlier track should end, then silence of about %s, "
                     "and `cd/%s` should start with music at 0:00.00 (not in the middle of a piece, not with a cut-off beginning)."
                     % (before["number"], after["number"], after["start"], before["file"], timecode(max(0, n_before - 12 * RATE)),
                        timecode(n_before), timecode(next((d["length"] for d in details if d["boundary"] == after["start"]), 0)), after["file"]))
    others = [c for c in candidates if c[0] not in used]
    if others:
        lines += ["", "Silence runs that were not used as a boundary (a silence inside a track, or a further boundary):"]
        for start, length in sorted(others):
            lines.append("- sector %d (%s from the start of the audio area), %s long, inside track %d at %s."
                         % (start, timecode(start - audio_start), timecode(length),
                            max(t["number"] for t in audio if t["start"] <= start), timecode(start - max(t["start"] for t in audio if t["start"] <= start))))
    return "\n".join(lines) + "\n"


# ---- main ------------------------------------------------------------------------------------

def split_image(image, count):
    data_sectors = find_data_end(image)
    if data_sectors == 0 or data_sectors >= image.sectors:
        raise ValueError("no data track or no audio after it")
    runs = silence_runs(image, data_sectors)
    boundaries, details, candidates = choose_boundaries(runs, data_sectors, image.sectors, count - 1,
                                                        energies=lambda: sector_energies(image, data_sectors))
    if len(boundaries) != count - 1:
        raise ValueError("found %d boundaries, %d needed" % (len(boundaries), count - 1))
    return data_sectors, runs, boundaries, details, candidates


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--image", required=True)
    ap.add_argument("--ffmpeg")
    ap.add_argument("--out", required=True)
    ap.add_argument("--report")
    ap.add_argument("--tracks", type=int, default=6, help="number of audio tracks (the game plays 2..6, track 7 is never played)")
    ap.add_argument("--no-encode", action="store_true", help="only the TOC and its report")
    args = ap.parse_args(argv)
    if not args.no_encode and not args.ffmpeg:
        ap.error("--ffmpeg is required unless --no-encode")
    out = Path(args.out)
    report = Path(args.report) if args.report else out
    out.mkdir(parents=True, exist_ok=True)
    report.mkdir(parents=True, exist_ok=True)
    image = Image(args.image)
    try:
        data_sectors, runs, boundaries, details, candidates = split_image(image, args.tracks)
        tracks = build_toc(image.sectors, data_sectors, boundaries)
        toc = {"frames_per_second": RATE, "sector_bytes": SECTOR, "image_sectors": image.sectors, "data_sectors": data_sectors,
               "audio_format": {"sample_rate": 44100, "channels": 2, "bits": 16},
               "opus": dict(OPUS_SETTINGS), "tracks": tracks}
        decoded = {}
        if not args.no_encode:
            for t in tracks:
                if t["type"] != "audio":
                    continue
                encode_track(args.ffmpeg, image.read(t["start"], t["length"]), t["length"], out / t["file"])
                decoded[t["number"]] = (decoded_samples(args.ffmpeg, out / t["file"]), t["length"] * OPUS_PER_SECTOR)
                print("track %02d: %s, %d bytes, decoded %d samples (TOC %d)" % (t["number"], t["file"], (out / t["file"]).stat().st_size,
                                                                               decoded[t["number"]][0], decoded[t["number"]][1]), flush=True)
                if decoded[t["number"]][0] != decoded[t["number"]][1]:
                    raise RuntimeError("track %d decodes to %d samples, the TOC says %d" % (t["number"], *decoded[t["number"]]))
        out.joinpath("toc.json").write_text(json.dumps(toc, indent=1) + "\n", newline="\n")
        report.joinpath("toc_report.json").write_text(json.dumps(
            {"silence_runs": [{"start": s, "length": n} for s, n in runs if n >= MIN_SILENCE], "boundaries": details,
             "tracks": tracks, "data_sectors": data_sectors, "image_sectors": image.sectors}, indent=1) + "\n", newline="\n")
        report.joinpath("toc_report.md").write_text(toc_report(Path(args.image).name, image.sectors, data_sectors, runs, details, candidates,
                                                                tracks, decoded or None), newline="\n")
    finally:
        image.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
