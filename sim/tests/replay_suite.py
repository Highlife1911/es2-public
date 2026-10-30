"""The own-process replay: one oracle suite through `es2sim.exe --headless`, compared with its golden.

For one suite of tools/reference-game/oracle-suites.json the driver

  1. prepares a working directory (the files the own process reads, section "Working directory" of
     docs/reference-game/oracle_regression.md, "The own-process replay"): the launch set (or the legacy C1_01
     set) in DATA\\, the CD's DATA, VOL, SAV and TAPES, ERROR.STR, DRIVE.CFG and LANGUAGE.CFG; nothing is written
     below the CD or into the repository,
  2. writes the INI of the run (the proxy's sections [trace] watches, [time], [input], [arming] plus the host's own
     [game] and [run]) and runs `es2sim.exe --headless --config <ini> --tick-log <log>` in that
     directory with the virtual-time settings every golden was recorded with (47 ms step, pace 0, intra-frame 1);
     the host reads no DBSIM.EXE (the guest image is built in), so no --exe is passed,
  3. normalizes the tick log with oracle_regression.py (the same code `check.ps1 -Live` uses) and compares it with
     the golden's normalized.json: tick fields, watches, return files (SHA-256) and, for suites that end by
     themselves, the exit code of the golden's manifest,
  4. prints one result line (IDENTICAL, DIFFERENT with the first difference, REFUSED with the host's named refusal,
     FAULT, SETUP-FAILED, NOT-RUN, TIMEOUT or SKIP) and deletes the working directory.

  replay_suite.py --sim <es2sim.exe> --suite NAME [--suite NAME ...] | --all
                  [--registry <oracle-suites.json>] [--oracle-script <oracle_regression.py>]
                  [--keep] [--output-dir <dir>] [--list] [--after-load-snapshot] [--frames]
                  [--dump-frames <list> --dump-dir <dir>] [--image-access]
                  [--window [--window-devices]] [--message-trace]

--frames adds `--frames <output-dir>/<suite>/frames.ndjson` to the command line: es2sim's frame capture,
compared with the frame references' manifest by `dbsim_frames.py compare` (not part of the verdict). es2sim then runs
with an environment of the frame references' size (frames_known.REFERENCE_ENVIRONMENT, section "The environment of a
frame capture" below).
--dump-frames LIST --dump-dir DIR add es2sim's `--dump-frames LIST --dump-dir DIR/<suite>`: ES2F dumps of the
named frames (numbers or from-to ranges), comparable with the proxy's (frame_dumps.py compare). They are decoded game
frames: DIR must be ignored scratch, and the dumps are deleted after use.
--image-access adds es2sim's `--image-access <output-dir>/<suite>/image-access.json`: the image-access
measurement (CODE no-access outside the table pages, the file-backed pages guarded) and its map of VA ranges and counts
(own_process.md, "what es2sim really reads from the mapped image"); the verdict does not depend on it.
--window runs the windowed build (--sim names es2sim_window.exe) without --headless: it replays the journal
with the window as presenter and the SOS mixer on an SDL audio device's wall time (main_loop.md, "the windowed notices measured, and the windowed replay"). By default
on SDL's dummy video and audio drivers (no visible window, no sound device; the dummy audio device takes the buffers in
wall time) with ES2SIM_MESSAGE_BOX=none; --window-devices keeps SDL's default drivers (a visible window and the real
sound device: live runs). The host's notice summary line is copied into result.json ("notices").
--message-trace sets ES2SIM_MESSAGE_TRACE=1: es2sim writes the message trace beside the tick log
(<output-dir>/<suite>/tick-log.jsonl.messages.ndjson); the verdict does not depend on it.

Environment: ES2_DATA_ROOT (the extracted CD, or the es2-data root above it), ES2_ORACLE_DIR (the directory with
golden/ and launch/; default: ES2_DATA_REPO\\oracle, else next to extracted/cd), ES2_REPLAY_TIMEOUT (seconds per suite, default 3300),
ES2_REPLAY_PREFS (a PREFS.CFG to use in place of the CD's). Without the CD or a golden the suite is SKIPped (exit 0).
Exit status: 0 for IDENTICAL and SKIP, 1 for every other result, 2 for a usage error.
"""
import argparse
import codecs
import hashlib
import importlib.util
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

# Every child process starts without a console window (//tools:process_starts_test).
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)

HERE = os.path.dirname(os.path.abspath(__file__))
LAUNCH_FILES = ("script.dat", "player.mec", "mission.var", "MISSION.STR")
RETURN_FILES = ("results.dat", "mission.var")
# The suites without a `launch` run on the legacy C1_01 state of the recording clones; it has to exist as a launch set
# of this name (docs: "Launch sets for the legacy suites").
LEGACY_LAUNCH = "c1_01-legacy"
# Every golden was recorded with these (run-oracle-regression.ps1: step 47, pace 0; configure-tick-log.ps1: start
# 16777216 and intra-frame 1 as defaults; the manifests of 38 of 40 goldens say intra-frame 1, the two oldest predate
# the field and replay identically with it).
VIRTUAL_TIME = {"virtual_time_step_ms": 47, "virtual_time_start_ms": 16777216, "virtual_time_pace": 0,
                "virtual_time_intra_frame": 1}
# Host exit codes (sim/include/es2/sim/host.h, dispatch.h).
EXIT_USAGE, EXIT_IMAGE, EXIT_CLAIM, EXIT_SETUP, EXIT_REFUSED, EXIT_FAULT = 64, 65, 66, 67, 70, 71
GUEST_FAULT_MIN = 0xC0000000   # a guest fault exits with the OS's exception code (own_process.md, "the original's fault paths")
TRAP_COST_SECONDS = 11.58e-6   # measured (own_process.md, "How a call reaches the dispatcher")

# The recording clones' DATA\results.dat (timing-r111, -p1, -p2, -p4, -p5: identical, measured; -p3 differs,
# as with the legacy launch set). SuiteLaunch.psm1 deletes the old result only under KeepReturns (a launch set with an
# outcome); every other run kept it, and a result the game rewrote with the same bytes is no output (the quit of
# c1_01-quit-dialog writes exactly these). The own process starts without a result, so that state is its baseline.
CLONE_RESULTS_SHA256 = "dc1cbabd3736efe4675455e7c2d1da951ba1f80dae37d76ccfd96d3b1d0f4b37"

VERDICT_OK = "IDENTICAL"


class Skip(Exception):
    """The suite cannot run here (no CD, no golden, no launch set): reported, not a failure."""


# ---------------------------------------------------------------- locating inputs

def find_cd(root):
    """The CD directory: `root` itself or root/extracted/cd, the one with the VOL directory the run copies (the host
    does not read VER95/DBSIM.EXE)."""
    if not root:
        return None
    for layout in ("", os.path.join("extracted", "cd")):
        candidate = os.path.normpath(os.path.join(root, layout))
        if os.path.isdir(os.path.join(candidate, "VOL")) and os.path.isdir(os.path.join(candidate, "DATA")):
            return candidate
    return None


def find_oracle_dir(root, override=None, repo=None):
    """es2-data\\oracle: ES2_ORACLE_DIR, else ES2_DATA_REPO\\oracle, else two levels above an extracted\\cd root
    (the older layout), else root\\oracle."""
    if override:
        return os.path.normpath(override)
    if repo:
        return os.path.join(os.path.normpath(repo), "oracle")
    if not root:
        return None
    norm = os.path.normpath(root)
    parent, leaf = os.path.split(norm)
    grand, middle = os.path.split(parent)
    if leaf.lower() == "cd" and middle.lower() == "extracted":
        return os.path.join(grand, "oracle")
    return os.path.join(norm, "oracle")


def load_registry(path):
    with open(path, encoding="utf-8") as stream:
        data = json.load(stream)
    return {s["name"]: s for s in data["suites"]}


def load_oracle_module(path):
    spec = importlib.util.spec_from_file_location("oracle_regression_for_replay", path)
    module = importlib.util.module_from_spec(spec)
    sys.path.insert(0, os.path.dirname(path))      # it imports mission_watch_report from its own directory
    try:
        spec.loader.exec_module(module)
    finally:
        sys.path.pop(0)
    return module


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def read_launch_set(oracle_dir, name):
    """Verifies a launch set like SuiteLaunch.psm1 Read-SuiteLaunch: schema 1, the name, exactly the four members with
    their recorded size and SHA-256. Returns (directory, {file: sha256})."""
    directory = os.path.join(oracle_dir, "launch", name)
    manifest_path = os.path.join(directory, "manifest.json")
    if not os.path.isfile(manifest_path):
        hint = ""
        if name == LEGACY_LAUNCH:
            hint = " (the coordinator creates it: python sim/tests/legacy_launch_set.py <clone>/DATA <oracle>/launch/%s)" % name
        raise Skip("launch set %s is not under %s%s" % (name, os.path.join(oracle_dir, "launch"), hint))
    with open(manifest_path, encoding="utf-8") as stream:
        manifest = json.load(stream)
    entries = manifest.get("files", [])
    if manifest.get("schema") != 1 or manifest.get("name") != name or sorted(e["file"] for e in entries) != sorted(LAUNCH_FILES):
        raise RuntimeError("launch set %s: invalid manifest (schema, name or members)" % name)
    hashes = {}
    for entry in entries:
        path = os.path.join(directory, entry["file"])
        if not os.path.isfile(path) or os.path.getsize(path) != entry["size"] or sha256_file(path).lower() != entry["sha256"].lower():
            raise RuntimeError("launch set %s: size or hash mismatch for %s" % (name, entry["file"]))
        hashes[entry["file"]] = entry["sha256"].lower()
    return directory, hashes


# ---------------------------------------------------------------- the plan of one suite

def watch_lines(path):
    with open(path, encoding="utf-8") as stream:
        return [line.strip() for line in stream if line.strip() and not line.lstrip().startswith("#")]


def build_plan(name, suite, tool_root):
    launch = suite.get("launch")
    return {
        "suite": name,
        "launch": launch or LEGACY_LAUNCH,
        "launch_kind": "launch" if launch else "legacy",
        "script": os.path.normpath(os.path.join(tool_root, suite["script"])),
        "watches": watch_lines(os.path.normpath(os.path.join(tool_root, suite["watches"]))),
        "ticks": suite["ticks"],
        "frame_limit": suite.get("frame_limit"),
        "outcome": bool(suite.get("outcome")),
        "game_arguments": list(suite.get("game_arguments") or []),
        "hold": bool(suite.get("hold")),
        "demo_tape": suite.get("demo_tape"),
    }


def playback(plan):
    """True for a -p or -D run (SuiteLaunch.psm1 Get-SuiteTapePlan): DBSIM overwrites the launch files from the tape."""
    return any(re.match(r"^[-/][pD]", a) for a in plan["game_arguments"])


def demo_list(name):
    """TAPES\\DEMOLIST.STR with one entry (SuiteLaunch.psm1 New-SuiteDemoList, docs/formats/str.md): u32 body size,
    i16 count 1, i16 length with the NUL, the text, the byte E = 0. With one entry 0x45ceb8 skips the wall-clock pick
    0x45cf87."""
    text = name.encode("ascii")
    return struct.pack("<Ihh", len(text) + 6, 1, len(text) + 1) + text + b"\0\0"


def config_text(plan, journal=None, snapshot=None, heap_trace=None):
    """The INI of one run. [trace] watches, [time], [input], [arming] are the proxy's sections written by
    configure-tick-log.ps1; [game] (extra DBSIM arguments) and [run] (where the host stops) are the host's own.
    With `snapshot` = (path, extra) also the proxy's [snapshot] (after_load=1, path, extra when given).
    With `heap_trace` (a path) also the host's [trace] heap_trace."""
    lines = ["[trace]", "tick_log_max=0", "tick_log_actor_raw=0", "watch_count=%d" % len(plan["watches"])]
    lines += ["watch%d=%s" % (i, w) for i, w in enumerate(plan["watches"], start=1)]
    if heap_trace:
        lines.append("heap_trace=%s" % heap_trace)
    lines += ["[time]"] + ["%s=%d" % kv for kv in VIRTUAL_TIME.items()]
    lines += ["[input]", "mode=replay", "journal=%s" % (journal or plan["script"]), "[arming]", "early=1"]
    if plan["game_arguments"]:
        lines += ["[game]", "arguments=%s" % " ".join(plan["game_arguments"])]
    lines += ["[run]", "ticks=%d" % plan["ticks"], "outcome=%d" % int(plan["outcome"])]
    if plan["frame_limit"] is not None:
        lines.append("frame_limit=%d" % plan["frame_limit"])
    elif plan.get("hold"):
        # The proxy's hold_frame parks DBSIM after `ticks` records; es2sim ends there.
        lines.append("frame_limit=%d" % plan["ticks"])
    if snapshot:
        lines += ["[snapshot]", "after_load=1", "path=%s" % snapshot[0]]
        if snapshot[1]:
            lines.append("extra=%s" % snapshot[1])
    return "\r\n".join(lines) + "\r\n"


def write_unicode(path, text):
    """UTF-16 LE with a byte order mark, as the proxy's INI (GetPrivateProfileInt reads it on any Windows)."""
    with open(path, "wb") as stream:
        stream.write(codecs.BOM_UTF16_LE + text.encode("utf-16-le"))


# ---------------------------------------------------------------- the working directory

def prepare_workdir(install, cd, launch_dir, plan, prefs_override=None):
    """The files the own process reads (a superset of what DBSIM opens, file_io.md): from the CD DATA, SAV, TAPES, VOL
    and GERMAN\\ERROR.GER as ERROR.STR; the four launch files over DATA; DRIVE.CFG (the CD root, then this directory)
    and LANGUAGE.CFG ("G", the language of every golden). Everything is copied, never linked: the game writes
    PREFS.CFG, mission.var and results.dat here."""
    os.makedirs(install)
    for folder in ("DATA", "SAV", "TAPES", "VOL"):
        source = os.path.join(cd, folder)
        if os.path.isdir(source):
            shutil.copytree(source, os.path.join(install, folder))
    os.makedirs(os.path.join(install, "DATA"), exist_ok=True)
    error = os.path.join(cd, "GERMAN", "ERROR.GER")
    if os.path.isfile(error):
        shutil.copyfile(error, os.path.join(install, "ERROR.STR"))
    for name in LAUNCH_FILES:
        shutil.copyfile(os.path.join(launch_dir, name), os.path.join(install, "DATA", name))
    if prefs_override:
        shutil.copyfile(prefs_override, os.path.join(install, "DATA", "PREFS.CFG"))
    with open(os.path.join(install, "DATA", "DRIVE.CFG"), "wb") as stream:
        stream.write(("%s\r\n%s\r\n" % (cd, install)).encode("ascii", "replace"))
    with open(os.path.join(install, "DATA", "LANGUAGE.CFG"), "wb") as stream:
        stream.write(b"G")
    if plan.get("demo_tape"):
        with open(os.path.join(install, "TAPES", "DEMOLIST.STR"), "wb") as stream:
            stream.write(demo_list(plan["demo_tape"]))
    # A stale result must not pass for the run's (SuiteLaunch.psm1 KeepReturns).
    stale = os.path.join(install, "DATA", "results.dat")
    if os.path.exists(stale):
        os.remove(stale)


def collect_outputs(install, outputs_dir, plan, start_hashes):
    """The return files as the oracle runner keeps them: each when it changed (results.dat against the clones' result,
    CLONE_RESULTS_SHA256), or both always for an outcome run of a launch set (SuiteLaunch.psm1: changed files, plus both
    under KeepReturns)."""
    os.makedirs(outputs_dir, exist_ok=True)
    kept = []
    keep_all = plan["outcome"] and plan["launch_kind"] == "launch"
    baseline = dict(start_hashes)
    if not keep_all:
        baseline["results.dat"] = CLONE_RESULTS_SHA256
    for name in RETURN_FILES:
        path = os.path.join(install, "DATA", name)
        if not os.path.isfile(path):
            continue
        changed = baseline.get(name) != sha256_file(path)
        # Playback overwrites mission.var from the tape: the runner keeps it only with a changed results.dat.
        if playback(plan) and name == "mission.var" and "results.dat" not in kept:
            continue
        if changed or keep_all:
            shutil.copyfile(path, os.path.join(outputs_dir, name))
            kept.append(name)
    return kept


# ---------------------------------------------------------------- the environment of a frame capture

# The heap trace (own_process.md, "the heap trace"): es2sim copies its environment onto the game's heap before the entry, as
# CW3220's attach does, so every later block lies higher by the copy's size, and a read past a block's end finds other
# bytes when that size differs from the original's. The frame references were captured with the original's
# environment; under --frames es2sim gets one with the same string count and copy size: the variables below, padded
# with synthetic ones. The replay verdict does not depend on it, and es2sim itself never pads.
FRAME_ENVIRONMENT_KEPT = ("SYSTEMROOT", "PATH", "TEMP", "TMP", "TZ")
FRAME_ENVIRONMENT_KEPT_PREFIXES = ("ES2SIM_", "SDL_")


def load_frame_environment():
    """(strings, bytes) of the frame references' environment copy: frames_known.REFERENCE_ENVIRONMENT, read from the
    frames_known.py next to this script."""
    spec = importlib.util.spec_from_file_location("frames_known_for_replay", os.path.join(HERE, "frames_known.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.REFERENCE_ENVIRONMENT


def environment_block(length):
    """The heap block of a copied string of `length` characters: CW3220's _malloc(length + 1) rounded up to 4 bytes,
    plus 8 bytes of header and footer, at least 16."""
    return max(16, ((length + 1 + 3) & ~3) + 8)


def environment_copy_size(env):
    """The bytes of the string blocks of `env`'s copy (without the pointer array, which depends on the count only)."""
    return sum(environment_block(len("%s=%s" % item)) for item in env.items())


def frame_environment(base, strings, size):
    """`base`'s kept variables plus Z000, Z001, ..., so that the copy has `strings` strings in `size` bytes of string
    blocks. RuntimeError when the kept variables alone do not fit."""
    env = {k: v for k, v in base.items()
           if k.upper() in FRAME_ENVIRONMENT_KEPT or k.upper().startswith(FRAME_ENVIRONMENT_KEPT_PREFIXES)}
    pads = strings - len(env)
    spare = size - environment_copy_size(env) - 16 * pads
    if pads < 0 or spare < 0 or spare % 4 or (pads == 0 and spare):
        raise RuntimeError("the kept environment (%d strings, %d bytes) does not fit the frame references' (%d strings, "
                           "%d bytes)" % (len(env), environment_copy_size(env), strings, size))
    each, rest = divmod(spare // 4, pads) if pads else (0, 0)
    for i in range(pads):
        block = 16 + 4 * (each + (i < rest))
        name = "Z%03d" % i
        env[name] = "x" * (block - 9 - len(name) - 1)   # "Z000=xx...": block - 9 characters
    return env


# ---------------------------------------------------------------- reading the run

def scan_log(path):
    """The host's own events without parsing every tick line: the first refusal and the exit record."""
    refused = exit_record = None
    ticks = 0
    if not os.path.isfile(path):
        return {"refused": None, "exit": None, "ticks": 0}
    with open(path, encoding="utf-8", errors="replace") as stream:
        for line in stream:
            if '"event":"tick"' in line or '"event": "tick"' in line:
                ticks += 1
            elif '"event":"refused"' in line or '"event": "refused"' in line:
                if refused is None:
                    refused = json.loads(line)
            elif '"event":"exit"' in line or '"event": "exit"' in line:
                exit_record = json.loads(line)
    return {"refused": refused, "exit": exit_record, "ticks": ticks}


def first_difference(report):
    """(frame or None, field, text) of the earliest difference in a compare() report."""
    best = None
    for p in report["problems"]:
        kind = p["kind"]
        frame, field, text = None, kind, ""
        if kind == "tick_field":
            frame, field = p["first_frame"], p["field"]
            text = "golden %s, run %s (%d ticks differ)" % (json.dumps(p["golden"]), json.dumps(p["run"]), p["count"])
        elif kind == "tick_frames":
            candidates = [f for f in (p["golden_only"][:1] + p["run_only"][:1])]
            frame, field = (min(candidates) if candidates else None), "tick_frames"
            text = "golden has %d ticks, run %d" % (p["golden_count"], p["run_count"])
        elif kind == "watches":
            entry = p["golden"] if p["golden"] is not None else p["run"]
            frame = entry[0] if entry else None
            field = "watch %s" % (entry[1] if entry else "?")
            text = "change %d of %d (golden has %d, run %d)" % (p["first_index"], max(p["golden_count"], p["run_count"]), p["golden_count"], p["run_count"])
        elif kind == "outputs":
            names = sorted(set(p["golden"]) | set(p["run"]))
            field = "return files"
            text = "golden %s, run %s" % (sorted(p["golden"]) or "none", sorted(p["run"]) or "none")
            text += "; differing: %s" % ", ".join(n for n in names if p["golden"].get(n) != p["run"].get(n))
        elif kind == "exit_code":
            field = "exit code"
            text = "golden %s, run %s" % (p["golden"], p["run"])
        else:
            text = json.dumps(p)[:160]
        key = (frame if frame is not None else 1 << 60)
        if best is None or key < best[0]:
            best = (key, frame, field, text)
    return best[1:] if best else (None, "", "")


def golden_exit(manifest):
    """(state, code) of the golden manifest's own_process_exit; ('absent', None) for goldens without that record."""
    record = (manifest or {}).get("own_process_exit") or {}
    state = record.get("exit_code_state")
    return (state, record.get("exit_code")) if state else ("absent", None)


def hot_calls(exit_record, limit=3):
    calls = (exit_record or {}).get("calls") or []
    return sorted(calls, key=lambda c: -c[1])[:limit]


# ---------------------------------------------------------------- one suite

def run_suite(name, suite, ctx):
    """Runs and judges one suite. ctx: sim (argv prefix), cd, oracle_dir, tool_root, oracle (module), output_dir,
    timeout, keep, prefs. Returns a result dict with at least suite, verdict, line."""
    result = {"suite": name, "verdict": "SKIP", "line": ""}
    started = time.time()
    plan = build_plan(name, suite, ctx["tool_root"])
    golden_dir = os.path.join(ctx["oracle_dir"], "golden", name)
    golden_path = os.path.join(golden_dir, "normalized.json")
    try:
        if not os.path.isfile(golden_path):
            raise Skip("no golden %s" % golden_path)
        launch_dir, start_hashes = read_launch_set(ctx["oracle_dir"], plan["launch"])
        for needed in (plan["script"],):
            if not os.path.isfile(needed):
                raise RuntimeError("input journal missing: %s" % needed)
        result.update(_run_prepared(name, plan, ctx, golden_dir, golden_path, launch_dir, start_hashes))
    except Skip as skip:
        result.update({"verdict": "SKIP", "line": "replay %s: SKIP %s" % (name, skip)})
    except RuntimeError as error:
        result.update({"verdict": "ERROR", "line": "replay %s: ERROR %s" % (name, error)})
    result["wall_seconds"] = round(time.time() - started, 2)
    return result


def _run_prepared(name, plan, ctx, golden_dir, golden_path, launch_dir, start_hashes):
    out_dir = os.path.join(ctx["output_dir"], name)
    os.makedirs(out_dir, exist_ok=True)
    os.makedirs(ctx["work_root"], exist_ok=True)
    work = tempfile.mkdtemp(prefix="replay-%s-" % name, dir=ctx["work_root"])
    install = os.path.join(work, "install")
    log_path = os.path.join(out_dir, "tick-log.jsonl")
    ini_path = os.path.join(out_dir, "es2-replay.ini")
    if os.path.exists(log_path):
        os.remove(log_path)      # the host opens it CREATE_NEW
    snapshot = None
    if ctx.get("snapshot"):
        snapshot = (os.path.join(out_dir, "es2sim-after-load.snap"), ctx.get("snapshot_extra") or "")
        if os.path.exists(snapshot[0]):
            os.remove(snapshot[0])   # the writer never overwrites a file
    frames = os.path.join(out_dir, "frames.ndjson") if ctx.get("frames") else None
    heap_trace = os.path.join(out_dir, "heap-trace.ndjson") if ctx.get("heap_trace") else None
    if heap_trace and os.path.exists(heap_trace):
        os.remove(heap_trace)    # the host creates it new
    image_access = os.path.join(out_dir, "image-access.json") if ctx.get("image_access") else None
    if image_access and os.path.exists(image_access):
        os.remove(image_access)
    if frames and os.path.exists(frames):
        os.remove(frames)
    try:
        prepare_workdir(install, ctx["cd"], launch_dir, plan, ctx.get("prefs"))
        write_unicode(ini_path, config_text(plan, snapshot=snapshot, heap_trace=heap_trace))
        command = list(ctx["sim"]) + ([] if ctx.get("window") else ["--headless"]) + ["--config", ini_path, "--tick-log", log_path]
        env = None
        if ctx.get("window") or ctx.get("message_trace"):
            env = dict(os.environ)
            if ctx.get("window") and not ctx.get("window_devices"):
                # No visible window and no sound device (SDL3's names and the older ones).
                for key in ("SDL_VIDEO_DRIVER", "SDL_VIDEODRIVER", "SDL_AUDIO_DRIVER", "SDL_AUDIODRIVER"):
                    env[key] = "dummy"
                env["ES2SIM_MESSAGE_BOX"] = "none"
            if ctx.get("message_trace"):
                trace = log_path + ".messages.ndjson"
                if os.path.exists(trace):
                    os.remove(trace)   # the host creates it new
                env["ES2SIM_MESSAGE_TRACE"] = "1"
        if frames:
            command += ["--frames", frames]
            env = frame_environment(os.environ if env is None else env, *ctx["frame_environment"])
        if ctx.get("dump_frames"):
            dump_dir = os.path.abspath(os.path.join(ctx["dump_dir"], name))
            os.makedirs(dump_dir, exist_ok=True)
            command += ["--dump-frames", ctx["dump_frames"], "--dump-dir", dump_dir]
        if image_access:
            command += ["--image-access", image_access]
        begin = time.time()
        try:
            completed = subprocess.run(command, cwd=install, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       timeout=ctx["timeout"], creationflags=NO_WINDOW, env=env)
            code, out, err = completed.returncode, completed.stdout, completed.stderr
        except subprocess.TimeoutExpired as expired:
            code, out, err = None, expired.stdout or b"", expired.stderr or b""
        except OSError as error:
            raise RuntimeError("cannot start %s: %s" % (command[0], error))
        run_seconds = time.time() - begin
        _write(out_dir, "host-stdout.txt", out)
        _write(out_dir, "host-stderr.txt", err)
        scan = scan_log(log_path)
        outputs_dir = os.path.join(out_dir, "outputs")
        shutil.rmtree(outputs_dir, ignore_errors=True)
        kept = collect_outputs(install, outputs_dir, plan, start_hashes)
    finally:
        if ctx["keep"]:
            kept_dir = os.path.join(out_dir, "install")
            shutil.rmtree(kept_dir, ignore_errors=True)
            shutil.move(install, kept_dir)
        shutil.rmtree(work, ignore_errors=True)
    stderr_text = err.decode("utf-8", "replace")
    exit_record = scan["exit"]
    timing = {"run_seconds": round(run_seconds, 2), "ticks": scan["ticks"]}
    if exit_record:
        trapped = exit_record.get("trapped_calls", 0)
        timing.update({"trapped_calls": trapped, "trap_seconds_estimate": round(trapped * TRAP_COST_SECONDS, 2),
                       "hot_calls": hot_calls(exit_record)})
    base = {"plan": {k: plan[k] for k in ("launch", "launch_kind", "frame_limit", "ticks", "outcome", "game_arguments")},
            "host_exit_code": code, "timing": timing, "outputs": kept, "output_dir": out_dir}
    notices = notice_line(out)
    if notices:
        base["notices"] = notices
    text = _judge(name, plan, ctx, golden_dir, golden_path, outputs_dir, log_path, code, scan, stderr_text, timing)
    base.update(text)
    with open(os.path.join(out_dir, "result.json"), "w", encoding="utf-8", newline="\n") as stream:
        json.dump(base, stream, indent=1)
    return base


def notice_line(stdout):
    """The windowed host's notice summary ("es2sim: notices: ..."), or None."""
    for line in (stdout or b"").decode("utf-8", "replace").splitlines():
        if line.startswith("es2sim: notices: "):
            return line[len("es2sim: notices: "):].strip()
    return None


def _write(directory, name, data):
    with open(os.path.join(directory, name), "wb") as stream:
        stream.write(data or b"")


def _judge(name, plan, ctx, golden_dir, golden_path, outputs_dir, log_path, code, scan, stderr_text, timing):
    prefix = "replay %s: " % name
    tail = " [%d ticks, %.1f s]" % (scan["ticks"], timing["run_seconds"])
    if code is None:
        return {"verdict": "TIMEOUT", "line": prefix + "TIMEOUT after %d s with %d ticks logged%s" % (ctx["timeout"], scan["ticks"], tail)}
    if code == EXIT_FAULT:
        fault = next((l for l in stderr_text.splitlines() if l.startswith("FAULT")), "unhandled fault")
        return {"verdict": "FAULT", "line": prefix + "FAULT %s%s" % (fault.strip(), tail)}
    refused = scan["refused"]
    if refused is None and code == EXIT_REFUSED:
        # The tick log may not exist (set-up refused before it): the stderr line carries the same record.
        for line in stderr_text.splitlines():
            if line.startswith('{"event":"refused"'):
                refused = json.loads(line)
                break
    if refused is not None or code == EXIT_REFUSED:
        refused = refused or {}
        pieces = ["REFUSED %s" % refused.get("va", "?")]
        if refused.get("name"):
            pieces.append('"%s"' % refused["name"])
        pieces.append("- %s" % (refused.get("reason") or "no reason given"))
        if refused.get("return"):
            pieces.append("(called from %s)" % refused["return"])
        if refused.get("index"):
            pieces.append("see %s" % refused["index"])
        return {"verdict": "REFUSED", "refusal": refused, "line": prefix + " ".join(pieces) + tail}
    if code in (EXIT_USAGE, EXIT_IMAGE, EXIT_CLAIM, EXIT_SETUP):
        message = next((l.strip() for l in reversed(stderr_text.splitlines()) if l.strip()), "no message")
        return {"verdict": "SETUP-FAILED", "line": prefix + "SETUP-FAILED host exit %d: %s" % (code, message)}
    with open(golden_path, encoding="utf-8") as stream:
        golden = json.load(stream)
    manifest_path = os.path.join(golden_dir, "manifest.json")
    manifest = None
    if os.path.isfile(manifest_path):
        with open(manifest_path, encoding="utf-8") as stream:
            manifest = json.load(stream)
    state, golden_code = golden_exit(manifest)
    # D: a guest fault ends the run as the original's unhandled fault does (the OS's code, >= 0xC0000000, and the host's
    # "FAULT: guest exception" line). A golden that exited with the same code is compared as usual; otherwise it is the
    # verdict FAULT, not a difference in the logs.
    guest_fault = next((l.strip() for l in stderr_text.splitlines() if l.startswith("FAULT: guest exception")), None)
    if guest_fault and code >= GUEST_FAULT_MIN and not (state == "exited" and golden_code == code):
        return {"verdict": "FAULT", "line": prefix + "FAULT %s (golden: exit code %s)%s" % (
            guest_fault, state if golden_code is None else "%s %s" % (state, golden_code), tail)}
    if scan["ticks"] == 0:
        return {"verdict": "NOT-RUN", "line": prefix + "NOT-RUN the host logged no ticks and exited with %d (golden: %d ticks, exit code %s)%s" % (
            code, golden["tick_count"], state if golden_code is None else "%s %s" % (state, golden_code), tail)}
    oracle = ctx["oracle"]
    normalized = oracle.normalize(log_path, outputs_dir, plan["frame_limit"])
    report = oracle.compare(golden, normalized)
    # The exit code is compared only for suites the original ended by itself (state "exited"); a golden that was
    # stopped by the runner ("none") has no game exit code, and a golden without the exit record has no state at all.
    if state == "exited" and golden_code != code:
        report["problems"].append({"kind": "exit_code", "golden": golden_code, "run": code})
        report["identical"] = False
    if report["identical"]:
        detail = "%d ticks, %d watch changes, %d return files, exit code %s (%s)" % (
            report["run_ticks"], report["run_watch_changes"], len(normalized["outputs"]), code, "compared" if state == "exited" else "not compared: golden state %s" % state)
        return {"verdict": VERDICT_OK, "line": prefix + "IDENTICAL " + detail + tail}
    frame, field, text = first_difference(report)
    where = "tick %s " % frame if frame is not None else ""
    kinds = sorted({p["kind"] if p["kind"] != "tick_field" else "tick_field:" + p["field"] for p in report["problems"]})
    return {"verdict": "DIFFERENT", "report": report,
            "line": prefix + "DIFFERENT first difference %sfield %s: %s; %d problems (%s)%s" % (
                where, field, text, len(report["problems"]), ", ".join(kinds[:6]), tail)}


# ---------------------------------------------------------------- command line

def summarize(results):
    counts = {}
    for r in results:
        counts[r["verdict"]] = counts.get(r["verdict"], 0) + 1
    return "replay summary: %d suites; %s" % (len(results), ", ".join("%s %d" % kv for kv in sorted(counts.items())))


def default_paths():
    root = os.path.normpath(os.path.join(HERE, "..", ".."))
    return (os.path.join(root, "tools", "reference-game", "oracle-suites.json"),
            os.path.join(root, "tools", "reference-game", "scripts", "oracle_regression.py"))


def main(argv=None):
    registry_default, oracle_default = default_paths()
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--sim", help="es2sim.exe")
    parser.add_argument("--suite", action="append", default=[])
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--registry", default=registry_default)
    parser.add_argument("--oracle-script", default=oracle_default)
    parser.add_argument("--output-dir", help="where tick logs, INIs, host output and result.json go (default: TEST_UNDECLARED_OUTPUTS_DIR or TEST_TMPDIR, else a temporary directory)")
    parser.add_argument("--keep", action="store_true", help="keep the working directory below --output-dir")
    parser.add_argument("--after-load-snapshot", action="store_true",
                        help="es2sim writes <output-dir>/<suite>/es2sim-after-load.snap before the first logic "
                             "frame (mission_load.md section 14)")
    parser.add_argument("--snapshot-extra", default="", help="[snapshot] extra of that snapshot: 0xVA:LEN,... (at most 16)")
    parser.add_argument("--frames", action="store_true",
                        help="es2sim writes its frame capture to <output-dir>/<suite>/frames.ndjson, "
                             "for dbsim_frames.py compare against the reference manifest")
    parser.add_argument("--dump-frames", default="",
                        help="es2sim dumps these frames (e.g. 1091,1100-1102) as ES2F files into --dump-dir/<suite>")
    parser.add_argument("--dump-dir", default="", help="the directory of the dumps (ignored scratch only)")
    parser.add_argument("--image-access", action="store_true",
                        help="es2sim writes its image-access map to <output-dir>/<suite>/image-access.json")
    parser.add_argument("--window", action="store_true",
                        help="the windowed build's replay (no --headless; SDL's dummy drivers by default)")
    parser.add_argument("--window-devices", action="store_true",
                        help="with --window, SDL's default drivers (a visible window, the real sound device)")
    parser.add_argument("--message-trace", action="store_true",
                        help="ES2SIM_MESSAGE_TRACE=1, the message trace beside the tick log")
    parser.add_argument("--heap-trace", action="store_true",
                        help="[trace] heap_trace, the heap trace heap-trace.ndjson beside the tick log")
    args = parser.parse_args(argv)
    if args.window_devices and not args.window:
        parser.error("--window-devices needs --window")
    if bool(args.dump_frames) != bool(args.dump_dir):
        parser.error("--dump-frames and --dump-dir go together")
    suites = load_registry(args.registry)
    if args.list:
        print("\n".join(suites))
        return 0
    names = list(suites) if args.all else args.suite
    if not names or not args.sim:
        parser.error("--sim and --suite NAME (or --all) are required")
    unknown = [n for n in names if n not in suites]
    if unknown:
        parser.error("not in the registry: %s" % ", ".join(unknown))
    root = os.environ.get("ES2_DATA_ROOT", "")
    cd = find_cd(root)
    oracle_dir = find_oracle_dir(root, os.environ.get("ES2_ORACLE_DIR"), os.environ.get("ES2_DATA_REPO"))
    if not cd or not oracle_dir or not os.path.isdir(os.path.join(oracle_dir, "golden")):
        for name in names:
            print("replay %s: SKIP no CD below ES2_DATA_ROOT or no oracle directory (CD %s, oracle %s)" % (name, cd, oracle_dir))
        return 0
    scratch = os.environ.get("TEST_TMPDIR") or tempfile.gettempdir()
    output_dir = args.output_dir or os.environ.get("TEST_UNDECLARED_OUTPUTS_DIR") or os.path.join(scratch, "replay-results")
    sim = [args.sim]
    ctx = {"sim": sim, "cd": cd, "oracle_dir": oracle_dir, "tool_root": os.path.dirname(os.path.abspath(args.registry)),
           "oracle": load_oracle_module(os.path.abspath(args.oracle_script)), "output_dir": output_dir, "work_root": scratch,
           "timeout": int(os.environ.get("ES2_REPLAY_TIMEOUT", "3300")), "keep": args.keep,
           "prefs": os.environ.get("ES2_REPLAY_PREFS") or None,
           "snapshot": args.after_load_snapshot, "snapshot_extra": args.snapshot_extra,
           "frames": args.frames, "frame_environment": load_frame_environment() if args.frames else None,
           "dump_frames": args.dump_frames, "dump_dir": args.dump_dir,
           "image_access": args.image_access, "window": args.window, "window_devices": args.window_devices,
           "message_trace": args.message_trace, "heap_trace": args.heap_trace}
    os.makedirs(output_dir, exist_ok=True)
    results = []
    for name in names:
        result = run_suite(name, suites[name], ctx)
        print(result["line"], flush=True)
        if result.get("notices"):
            print("replay %s: notices %s" % (name, result["notices"]), flush=True)
        results.append(result)
    if len(results) > 1:
        print(summarize(results))
    return 0 if all(r["verdict"] in (VERDICT_OK, "SKIP") for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
