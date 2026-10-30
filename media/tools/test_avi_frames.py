# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""avi_frames.exe on synthetic uncompressed AVIs and on the real intro movies.

usage: test_avi_frames.py <avi_frames.exe> [<ffmpeg.exe>]

The synthetic files carry BI_RGB video (24-bit and 8-bit with a colour table) and PCM sound, so
Video for Windows hands the DIBs through without a codec; the expected bytes are known exactly.
The real movies (ES2_DATA_ROOT) go through the bound system Indeo codec; when FFmpeg is given,
the frames are compared with FFmpeg's own Indeo 3 decoder (the two differ in the colour
conversion only). Both parts skip without their inputs.
"""
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

EXE = Path(sys.argv[1]).resolve()
FFMPEG = Path(sys.argv[2]) if len(sys.argv) > 2 else None
checks = 0


def check(value, detail=""):
    global checks
    checks += 1
    if not value:
        raise AssertionError(detail)


def chunk(tag, body):
    return tag + struct.pack("<I", len(body)) + body + (b"\0" if len(body) & 1 else b"")


def lst(kind, body):
    return chunk(b"LIST", kind + body)


def dib_rows(width, height, bits, pixel):
    """Bottom-up rows of a DIB; pixel(x, y) is the file's (top-down) pixel."""
    stride = (width * bits + 31) // 32 * 4
    rows = []
    for y in range(height - 1, -1, -1):
        row = bytearray(stride)
        for x in range(width):
            p = pixel(x, y)
            if bits == 24:
                row[3 * x:3 * x + 3] = bytes(p[::-1])
            else:
                row[x] = p
        rows.append(bytes(row))
    return b"".join(rows)


def make_avi(width, height, bits, frames, audio, palette=None):
    """frames: list of DIB bodies; audio: list of byte strings (8-bit mono, 8000 Hz) or None."""
    colors = len(palette) if palette else 0
    bih = struct.pack("<IiiHHIIiiII", 40, width, height, 1, bits, 0, 0, 0, 0, colors, 0)
    if palette:
        bih += b"".join(bytes((b, g, r, 0)) for r, g, b in palette)
    strl_video = lst(b"strl", chunk(b"strh", struct.pack("<4s4sIHHIIIIIIIIhhhh", b"vids", b"DIB ", 0, 0, 0, 0, 1, 10, 0, len(frames), 0, 0xffffffff, 0, 0, 0, width, height)) + chunk(b"strf", bih))
    streams = 1
    hdrl = b""
    movi = b""
    index = b""
    offset = 4
    audio_bytes = 0
    for number, frame in enumerate(frames):
        entries = [(b"00db", frame)]
        if audio:
            entries.append((b"01wb", audio[number]))
            audio_bytes += len(audio[number])
        for tag, body in entries:
            piece = chunk(tag, body)
            index += struct.pack("<4sIII", tag, 0x10, offset, len(body))
            movi += piece
            offset += len(piece)
    strl = strl_video
    if audio:
        streams = 2
        wave = struct.pack("<HHIIHHH", 1, 1, 8000, 8000, 1, 8, 0)
        strl += lst(b"strl", chunk(b"strh", struct.pack("<4s4sIHHIIIIIIIIhhhh", b"auds", b"\0\0\0\0", 0, 0, 0, 0, 1, 8000, 0, audio_bytes, 0, 0xffffffff, 1, 0, 0, 0, 0)) + chunk(b"strf", wave))
    avih = struct.pack("<IIIIIIIIII4I", 100000, 0, 0, 0x10, len(frames), 0, streams, 0, width, height, 0, 0, 0, 0)
    hdrl = lst(b"hdrl", chunk(b"avih", avih) + strl)
    body = b"AVI " + hdrl + lst(b"movi", movi) + chunk(b"idx1", index)
    return b"RIFF" + struct.pack("<I", len(body)) + body


def run(args):
    return subprocess.run([str(EXE)] + args, capture_output=True, text=True, timeout=120)


def test_rgb24(tmp):
    w, h = 6, 4
    def pixel(n):
        return lambda x, y: ((x * 40 + n * 7) & 255, (y * 60 + n) & 255, (x * y * 9 + n * 100) & 255)
    frames = [dib_rows(w, h, 24, pixel(n)) for n in range(3)]
    audio = [bytes(range(n * 10, n * 10 + 80)) for n in range(3)]
    (tmp / "rgb.avi").write_bytes(make_avi(w, h, 24, frames, audio))
    r = run([str(tmp / "rgb.avi"), str(tmp / "rgb")])
    check(r.returncode == 0, r.stderr)
    meta = json.loads((tmp / "rgb.json").read_text())
    check((meta["width"], meta["height"], meta["frames"]) == (w, h, 3), meta)
    check((meta["rate_num"], meta["rate_den"]) == (10, 1), meta)
    check(meta["pixel_format"] == "rgb24" and meta["source_bits"] == 24, meta)
    expected = b"".join(bytes(c for y in range(h) for x in range(w) for c in pixel(n)(x, y)) for n in range(3))
    check((tmp / "rgb.rgb").read_bytes() == expected, "RGB24 frames differ")
    wav = (tmp / "rgb.wav").read_bytes()
    check(wav[:4] == b"RIFF" and wav[8:16] == b"WAVEfmt " and len(wav) == 44 + 240, len(wav))
    check(struct.unpack("<HHIIHH", wav[20:36]) == (1, 1, 8000, 8000, 1, 8), "WAV format")
    check(struct.unpack("<I", wav[40:44])[0] == 240 and struct.unpack("<I", wav[4:8])[0] == 36 + 240, "WAV sizes")
    check(wav[44:] == b"".join(audio), "PCM differs")
    check(meta["audio"] == {"format_tag": 1, "channels": 1, "sample_rate": 8000, "bits": 8, "samples": 240, "bytes": 240}, meta)
    # --max-frames cuts the frames and keeps the whole sound
    r = run([str(tmp / "rgb.avi"), str(tmp / "cut"), "--max-frames", "2"])
    check(r.returncode == 0, r.stderr)
    check((tmp / "cut.rgb").read_bytes() == expected[:2 * w * h * 3], "cut frames")
    check(json.loads((tmp / "cut.json").read_text())["frames"] == 2, "cut count")


def test_palette(tmp):
    w, h = 5, 3
    palette = [(i * 3 & 255, 255 - i, i * 7 & 255) for i in range(16)]
    frames = [dib_rows(w, h, 8, lambda x, y, n=n: (x + y * 5 + n) % 16) for n in range(2)]
    (tmp / "pal.avi").write_bytes(make_avi(w, h, 8, frames, None, palette))
    r = run([str(tmp / "pal.avi"), str(tmp / "pal")])
    check(r.returncode == 0, r.stderr)
    meta = json.loads((tmp / "pal.json").read_text())
    check(meta["source_bits"] == 8 and meta["frames"] == 2 and "audio" not in meta, meta)
    expected = b"".join(bytes(c for y in range(h) for x in range(w) for c in palette[(x + y * 5 + n) % 16]) for n in range(2))
    check((tmp / "pal.rgb").read_bytes() == expected, "palette expansion differs")
    check(not (tmp / "pal.wav").exists(), "WAV without audio stream")


def test_errors(tmp):
    r = run([])
    check(r.returncode == 2 and "usage" in r.stderr, "usage")
    r = run([str(tmp / "missing.avi"), str(tmp / "x")])
    check(r.returncode == 1, "missing input")
    (tmp / "junk.avi").write_bytes(b"RIFF\x08\0\0\0AVI JUNK")
    r = run([str(tmp / "junk.avi"), str(tmp / "x")])
    check(r.returncode == 1, "junk input")
    r = run([str(tmp / "pal.avi"), str(tmp / "x"), "--max-frames", "0"])
    check(r.returncode == 1, "bad --max-frames")


def psnr(a, b):
    total = sum((x - y) * (x - y) for x, y in zip(a, b))
    return 99.0 if total == 0 else 10 * math.log10(255 * 255 * len(a) / total)


def test_real(tmp):
    root = os.environ.get("ES2_DATA_ROOT")
    movie = Path(root) / "AVI" / "INTR_PT1.AVI" if root else None
    if not movie or not movie.is_file():
        print("SKIP real movie: ES2_DATA_ROOT has no AVI/INTR_PT1.AVI")
        return
    r = run([str(movie), str(tmp / "intro"), "--max-frames", "6"])
    if r.returncode != 0 and "no decompressor" in r.stderr:
        print("SKIP real movie: no system Indeo wrapper")
        return
    check(r.returncode == 0, r.stderr)
    meta = json.loads((tmp / "intro.json").read_text())
    check((meta["width"], meta["height"], meta["frames"], meta["stream_frames"]) == (288, 180, 6, 604), meta)
    check((meta["rate_num"], meta["rate_den"]) == (15, 1) and meta["source_compression"] == "IV32", meta)
    check(meta["audio"]["channels"] == 2 and meta["audio"]["sample_rate"] == 11025 and meta["audio"]["bits"] == 8, meta)
    frames = (tmp / "intro.rgb").read_bytes()
    check(len(frames) == 6 * 288 * 180 * 3 and max(frames) > 64, "decoded frames")
    check((tmp / "intro.wav").stat().st_size == 44 + meta["audio"]["bytes"], "WAV size")
    if not FFMPEG or not FFMPEG.is_file():
        print("SKIP FFmpeg cross-check: no ffmpeg")
        return
    other = tmp / "ffmpeg.rgb"
    r = subprocess.run([str(FFMPEG), "-v", "error", "-y", "-i", str(movie), "-frames:v", "6", "-f", "rawvideo", "-pix_fmt", "rgb24", str(other)],
                       capture_output=True, text=True, timeout=120)
    check(r.returncode == 0, r.stderr)
    reference = other.read_bytes()
    check(len(reference) == len(frames), "FFmpeg frame count")
    # The same pictures decoded twice; the converters round differently.
    value = psnr(frames, reference)
    print("windows decode vs ffmpeg indeo3: %.1f dB" % value)
    check(value >= 35, "Windows decode differs from FFmpeg's Indeo 3: %.1f dB" % value)


def main():
    with tempfile.TemporaryDirectory(dir=os.environ.get("TEST_TMPDIR")) as name:
        tmp = Path(name)
        test_rgb24(tmp)
        test_palette(tmp)
        test_errors(tmp)
        test_real(tmp)
    print("test_avi_frames: PASS (%d checks)" % checks)


main()
