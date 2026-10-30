"""Unit test of the replay driver replay_suite.py on synthetic data: a stub es2sim, a tiny fake CD, launch sets
and goldens built here. No original data is read and no game is started.

  replay_suite_test.py <oracle-suites.json> <oracle_regression.py> <replay_suites.bzl> <replay_stub_sim.py>
"""
import json
import os
import re
import shutil
import sys
import tempfile
import unittest
import unittest.mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import legacy_launch_set  # noqa: E402
import replay_suite as rs  # noqa: E402

REGISTRY, ORACLE_SCRIPT, SUITES_BZL, STUB = (os.path.abspath(a) for a in sys.argv[1:5])
del sys.argv[1:]

ORACLE = rs.load_oracle_module(ORACLE_SCRIPT)
sys.path.insert(0, os.path.dirname(ORACLE_SCRIPT))


def tick(frame, rng="aa"):
    return ('{"event":"tick","sequence":%d,"qpc":0,"ebx":"0x0","frame_counter_before_increment":%d,'
            '"completed_clock_ms":%d,"limiter_bypassed":0,"loop_pause_flag":0,"clock_raw_hex":"00","rng_a_hex":"%s",'
            '"rng_b_hex":"bb","position_raw_i32":[1,2,3],"angles_raw_u16":[0,0,0],"speed_raw_i16":0,'
            '"throttle_raw_q10":0,"vt_limiter_polls":%d,"vt_stall_advances":0,"in_injected":0,"in_fallbacks":0}\r\n'
            % (frame + 1, frame, frame * 47, rng, frame + 1))


def tick_log(frames, rng_at=None, exit_code=None):
    text = '{"event":"start","schema":2,"mode":"own_process_headless","record_limit":0,"actor_raw":0,"watches":0}\r\n'
    for f in range(frames):
        text += tick(f, "ee" if rng_at == f else "aa")
    if exit_code is not None:
        text += ('{"event":"exit","reason":"mission_end","exit_code":%d,"frames":%d,"trapped_calls":2000,'
                 '"calls":[["0x0049630c",1500],["0x00401000",20],["0x00402000",480]]}\r\n' % (exit_code, frames))
    return text


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as stream:
        stream.write(data if isinstance(data, bytes) else data.encode())


class World:
    """A fake CD, an oracle directory (goldens, launch sets), a registry and its inputs under one temporary root."""

    def __init__(self, root):
        self.root = root
        self.cd = os.path.join(root, "extracted", "cd")
        self.oracle = os.path.join(root, "oracle")
        self.tools = os.path.join(root, "tools")
        for name in ("PREFS.CFG", "KEYJOY.CFG", "SOUND.CFG", "EXIT.CFG", "MAT0.DAT"):
            write(os.path.join(self.cd, "DATA", name), "cd " + name)
        write(os.path.join(self.cd, "VOL", "A.VOL"), b"volume")
        write(os.path.join(self.cd, "SAV", "GAMEFILE.STR"), b"sav")
        write(os.path.join(self.cd, "TAPES", "DEMOLIST.STR"), b"tapes")
        write(os.path.join(self.cd, "GERMAN", "ERROR.GER"), b"errors")
        write(os.path.join(self.tools, "input-scripts", "j.jsonl"), '{"f": 1}\n')
        write(os.path.join(self.tools, "watch-sets", "w.txt"), "mission_globals 0x4a9e20 80\nroute 0x4d24e6@0x45@6@0 6\n")
        self.suites = []

    def launch_set(self, name, mission_var=b"VAR0"):
        directory = os.path.join(self.oracle, "launch", name)
        files = {"script.dat": b"script", "player.mec": b"mec", "mission.var": mission_var, "MISSION.STR": b"str"}
        entries = []
        for file, data in files.items():
            write(os.path.join(directory, file), data)
            import hashlib
            entries.append({"file": file, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
        write(os.path.join(directory, "manifest.json"), json.dumps({"schema": 1, "name": name, "files": entries}))

    def suite(self, name, golden_log=None, frame_limit=None, outcome=False, launch=None, exit_state=None, exit_code=None,
              outputs=None, game_arguments=None):
        entry = {"name": name, "script": "input-scripts/j.jsonl", "watches": "watch-sets/w.txt", "ticks": 50,
                 "outcome": outcome}
        if launch:
            entry["launch"] = launch
        if frame_limit is not None:
            entry["frame_limit"] = frame_limit
        if game_arguments:
            entry["game_arguments"] = game_arguments
        self.suites.append(entry)
        if golden_log is not None:
            directory = os.path.join(self.oracle, "golden", name)
            log = os.path.join(self.root, name + "-golden.jsonl")
            write(log, golden_log)
            outputs_dir = None
            if outputs:
                outputs_dir = os.path.join(self.root, name + "-golden-out")
                for file, data in outputs.items():
                    write(os.path.join(outputs_dir, file), data)
            write(os.path.join(directory, "normalized.json"), json.dumps(ORACLE.normalize(log, outputs_dir, frame_limit)))
            manifest = {"suite": name}
            if exit_state:
                manifest["own_process_exit"] = {"exit_code": exit_code, "exit_code_state": exit_state}
            write(os.path.join(directory, "manifest.json"), json.dumps(manifest))
        write(os.path.join(self.tools, "oracle-suites.json"), json.dumps({"format": 3, "suites": self.suites}))
        return entry

    def context(self, scenario, record=None, timeout=60, **env):
        out = os.path.join(self.root, "results")
        os.makedirs(out, exist_ok=True)
        os.environ.update({"STUB_SCENARIO": scenario, "STUB_RECORD": record or "", **{k: str(v) for k, v in env.items()}})
        return {"sim": [sys.executable, "-B", STUB], "cd": self.cd, "oracle_dir": self.oracle, "tool_root": self.tools,
                "oracle": ORACLE, "output_dir": out, "work_root": self.root, "timeout": timeout, "keep": False, "prefs": None,
                "frame_environment": rs.load_frame_environment()}


class ReplayDriver(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="replay-test-")
        self.world = World(self.root)
        for key in ("STUB_SCENARIO", "STUB_RECORD", "STUB_LOG", "STUB_EXIT", "STUB_OUTPUTS", "STUB_NOTICES",
                    "SDL_VIDEO_DRIVER", "SDL_VIDEODRIVER", "SDL_AUDIO_DRIVER", "SDL_AUDIODRIVER", "ES2SIM_MESSAGE_BOX",
                    "ES2SIM_MESSAGE_TRACE"):
            os.environ.pop(key, None)

    def tearDown(self):
        shutil.rmtree(self.root, ignore_errors=True)

    def log_file(self, text):
        path = os.path.join(self.root, "stub-log.jsonl")
        write(path, text)
        return path

    def run_one(self, entry, ctx):
        return rs.run_suite(entry["name"], entry, ctx)

    def test_identical_outcome_suite_with_launch_set(self):
        w = self.world
        w.launch_set("lset")
        golden = tick_log(5, exit_code=3)
        # Golden return files: results.dat and mission.var (the launch + outcome rule keeps both).
        entry = w.suite("syn-launch", golden, outcome=True, launch="lset", exit_state="exited", exit_code=3,
                        outputs={"results.dat": b"RESULTS", "mission.var": b"MISSION-VAR-AFTER"})
        record = os.path.join(self.root, "record.json")
        ctx = w.context("identical", record, STUB_LOG=self.log_file(tick_log(5, exit_code=3)), STUB_OUTPUTS=1, STUB_EXIT=3)
        result = self.run_one(entry, ctx)
        self.assertEqual(result["verdict"], "IDENTICAL", result["line"])
        self.assertIn("exit code 3 (compared)", result["line"])
        self.assertEqual(result["timing"]["trapped_calls"], 2000)
        self.assertEqual(result["timing"]["hot_calls"][0][0], "0x0049630c")
        with open(record, encoding="utf-8") as stream:
            seen = json.load(stream)
        argv = seen["argv"]
        self.assertEqual(argv[0], "--headless")
        self.assertNotIn("--exe", argv)   # the host reads no DBSIM.EXE
        self.assertEqual(seen["ini_bom"], "fffe")
        for text in ("virtual_time_step_ms=47", "virtual_time_pace=0", "virtual_time_intra_frame=1", "mode=replay",
                     "watch_count=2", "watch1=mission_globals 0x4a9e20 80", "outcome=1", "ticks=50"):
            self.assertIn(text, seen["ini"])
        self.assertNotIn("frame_limit", seen["ini"])
        self.assertNotIn("[game]", seen["ini"])
        for name in ("DATA/script.dat", "DATA/player.mec", "DATA/mission.var", "DATA/MISSION.STR", "DATA/PREFS.CFG",
                     "DATA/DRIVE.CFG", "DATA/LANGUAGE.CFG", "VOL/A.VOL", "SAV/GAMEFILE.STR", "TAPES/DEMOLIST.STR", "ERROR.STR"):
            self.assertIn(name, seen["listing"])
        self.assertNotIn("DATA/results.dat", seen["listing"])
        self.assertEqual(seen["language_cfg"], "G")
        self.assertEqual(seen["drive_cfg"].split("\r\n")[0], w.cd)
        self.assertEqual(seen["drive_cfg"].split("\r\n")[1], seen["cwd"])
        self.assertEqual(bytes.fromhex(seen["mission_var"]), b"VAR0")
        self.assertFalse(os.path.exists(seen["cwd"]), "the working directory is deleted after the run")
        self.assertTrue(os.path.isfile(os.path.join(result["output_dir"], "result.json")))

    def test_frame_limit_legacy_suite_and_game_arguments(self):
        w = self.world
        w.launch_set(rs.LEGACY_LAUNCH)
        entry = w.suite("syn-legacy", tick_log(3), frame_limit=3, game_arguments=["-SPRUNKNOWN", "-x"])
        record = os.path.join(self.root, "record.json")
        # The run logs five ticks; the comparison window is the suite's frame_limit.
        ctx = w.context("identical", record, STUB_LOG=self.log_file(tick_log(5)))
        result = self.run_one(entry, ctx)
        self.assertEqual(result["verdict"], "IDENTICAL", result["line"])
        self.assertIn("not compared: golden state absent", result["line"])
        with open(record, encoding="utf-8") as stream:
            ini = json.load(stream)["ini"]
        self.assertIn("frame_limit=3", ini)
        self.assertIn("[game]\r\narguments=-SPRUNKNOWN -x", ini)
        self.assertEqual(result["plan"]["launch_kind"], "legacy")

    def test_environment_block(self):
        # Every run, with and without --frames: the frame references' environment as es2sim's --environment FILE;
        # --padded-environment (with --frames) passes it as the process's environment instead.
        w = self.world
        w.launch_set(rs.LEGACY_LAUNCH)
        entry = w.suite("syn-env", tick_log(3), frame_limit=3)
        record = os.path.join(self.root, "record.json")
        for frames in (False, True):
            ctx = w.context("identical", record, STUB_LOG=self.log_file(tick_log(3)))
            ctx.update({"frames": frames})
            result = self.run_one(entry, ctx)
            self.assertEqual(result["verdict"], "IDENTICAL", result["line"])
            with open(record, encoding="utf-8") as stream:
                argv = json.load(stream)["argv"]
            self.assertEqual("--frames" in argv, frames)
            block_path = argv[argv.index("--environment") + 1]
            self.assertEqual(os.path.basename(block_path), "environment.block")
            with open(block_path, "rb") as stream:
                block = stream.read()
            self.assertTrue(block.endswith(b"\0\0"))
            strings = block[:-1].split(b"\0")[:-1]
            names = [s.split(b"=", 1)[0].decode("ascii") for s in strings]
            self.assertEqual(names, sorted(names, key=str.upper))
            env = dict(s.decode("mbcs").split("=", 1) for s in strings)
            self.assertEqual((len(env), rs.environment_copy_size(env)), rs.load_frame_environment())
        ctx = w.context("identical", record, STUB_LOG=self.log_file(tick_log(3)))
        ctx.update({"frames": True, "padded_environment": True})
        seen = []
        real_run = rs.subprocess.run

        def run(command, **kwargs):
            seen.append((command, kwargs.get("env")))
            return real_run(command, **kwargs)
        with unittest.mock.patch.object(rs.subprocess, "run", run):
            self.run_one(entry, ctx)
        command, env = seen[0]
        self.assertNotIn("--environment", command)
        self.assertEqual((len(env), rs.environment_copy_size(env)), rs.load_frame_environment())
        with self.assertRaises(SystemExit):
            rs.main(["--sim", "x", "--suite", "syn-env", "--padded-environment"])

    def test_environment_block_bytes(self):
        self.assertEqual(rs.environment_block_bytes({"b": "1", "A": "2", "_X": "3"}), b"A=2\0b=1\0_X=3\0\0")

    def test_dump_frames_pass_through(self):
        # --dump-frames LIST --dump-dir DIR reach es2sim as --dump-frames LIST --dump-dir DIR/<suite>.
        w = self.world
        w.launch_set(rs.LEGACY_LAUNCH)
        entry = w.suite("syn-dump", tick_log(3), frame_limit=3)
        record = os.path.join(self.root, "record.json")
        ctx = w.context("identical", record, STUB_LOG=self.log_file(tick_log(3)))
        dumps = os.path.join(self.root, "dumps")
        ctx.update({"dump_frames": "2,5-6", "dump_dir": dumps})
        result = self.run_one(entry, ctx)
        self.assertEqual(result["verdict"], "IDENTICAL", result["line"])
        with open(record, encoding="utf-8") as stream:
            argv = json.load(stream)["argv"]
        self.assertEqual(argv[argv.index("--dump-frames") + 1], "2,5-6")
        self.assertEqual(argv[argv.index("--dump-dir") + 1], os.path.abspath(os.path.join(dumps, "syn-dump")))
        self.assertTrue(os.path.isdir(os.path.join(dumps, "syn-dump")))
        # without them nothing is added
        ctx = w.context("identical", record, STUB_LOG=self.log_file(tick_log(3)))
        self.run_one(entry, ctx)
        with open(record, encoding="utf-8") as stream:
            self.assertNotIn("--dump-frames", json.load(stream)["argv"])
        # one without the other is refused (argparse exits)
        with self.assertRaises(SystemExit):
            rs.main(["--sim", "x", "--suite", "syn-dump", "--dump-frames", "2"])

    def test_image_access_pass_through(self):
        # --image-access reaches es2sim as --image-access <output-dir>/<suite>/image-access.json.
        w = self.world
        w.launch_set(rs.LEGACY_LAUNCH)
        entry = w.suite("syn-access", tick_log(3), frame_limit=3)
        record = os.path.join(self.root, "record.json")
        ctx = w.context("identical", record, STUB_LOG=self.log_file(tick_log(3)))
        ctx["image_access"] = True
        result = self.run_one(entry, ctx)
        self.assertEqual(result["verdict"], "IDENTICAL", result["line"])
        with open(record, encoding="utf-8") as stream:
            argv = json.load(stream)["argv"]
        self.assertEqual(argv[argv.index("--image-access") + 1],
                         os.path.join(result["output_dir"], "image-access.json"))
        # without it nothing is added
        ctx = w.context("identical", record, STUB_LOG=self.log_file(tick_log(3)))
        self.run_one(entry, ctx)
        with open(record, encoding="utf-8") as stream:
            self.assertNotIn("--image-access", json.load(stream)["argv"])

    def test_window_and_message_trace_pass_through(self):
        # --window drops --headless and selects SDL's dummy drivers; --message-trace sets the trace switch and
        # removes an old trace; the host's notice line lands in result.json.
        w = self.world
        w.launch_set(rs.LEGACY_LAUNCH)
        entry = w.suite("syn-window", tick_log(3), frame_limit=3)
        record = os.path.join(self.root, "record.json")
        ctx = w.context("identical", record, STUB_LOG=self.log_file(tick_log(3)),
                        STUB_NOTICES="2 posted (0 lost), 2 retrieved")
        ctx.update({"window": True, "message_trace": True})
        result = self.run_one(entry, ctx)
        self.assertEqual(result["verdict"], "IDENTICAL", result["line"])
        self.assertEqual(result["notices"], "2 posted (0 lost), 2 retrieved")
        with open(record, encoding="utf-8") as stream:
            data = json.load(stream)
        self.assertNotIn("--headless", data["argv"])
        self.assertEqual(data["env"], {"SDL_VIDEO_DRIVER": "dummy", "SDL_VIDEODRIVER": "dummy", "SDL_AUDIO_DRIVER": "dummy",
                                       "SDL_AUDIODRIVER": "dummy", "ES2SIM_MESSAGE_BOX": "none",
                                       "ES2SIM_MESSAGE_TRACE": "1"})
        # --window-devices keeps SDL's own drivers; the trace alone keeps --headless
        os.environ.pop("STUB_NOTICES")
        ctx = w.context("identical", record, STUB_LOG=self.log_file(tick_log(3)))
        ctx.update({"window": True, "window_devices": True})
        self.run_one(entry, ctx)
        with open(record, encoding="utf-8") as stream:
            data = json.load(stream)
        self.assertNotIn("--headless", data["argv"])
        self.assertEqual(set(data["env"].values()), {None})
        ctx = w.context("identical", record, STUB_LOG=self.log_file(tick_log(3)))
        ctx["message_trace"] = True
        result = self.run_one(entry, ctx)
        with open(record, encoding="utf-8") as stream:
            data = json.load(stream)
        self.assertIn("--headless", data["argv"])
        self.assertEqual(data["env"]["ES2SIM_MESSAGE_TRACE"], "1")
        self.assertIsNone(data["env"]["SDL_VIDEODRIVER"])
        self.assertNotIn("notices", result)
        # without either nothing changes
        ctx = w.context("identical", record, STUB_LOG=self.log_file(tick_log(3)))
        self.run_one(entry, ctx)
        with open(record, encoding="utf-8") as stream:
            data = json.load(stream)
        self.assertIn("--headless", data["argv"])
        self.assertEqual(set(data["env"].values()), {None})
        # --window-devices alone is refused
        with self.assertRaises(SystemExit):
            rs.main(["--sim", "x", "--suite", "syn-window", "--window-devices"])
        self.assertEqual(rs.notice_line(b"es2sim: x\r\nes2sim: notices: 5 posted\r\n"), "5 posted")
        self.assertIsNone(rs.notice_line(b"es2sim: x\n"))

    def test_different_names_the_first_tick_and_field(self):
        w = self.world
        w.launch_set(rs.LEGACY_LAUNCH)
        entry = w.suite("syn-diff", tick_log(5))
        ctx = w.context("identical", None, STUB_LOG=self.log_file(tick_log(5, rng_at=3)))
        result = self.run_one(entry, ctx)
        self.assertEqual(result["verdict"], "DIFFERENT")
        self.assertIn("first difference tick 3 field rng_a_hex", result["line"])
        self.assertIn('golden "aa", run "ee"', result["line"])

    def test_shorter_run_and_missing_return_file_and_exit_code(self):
        w = self.world
        w.launch_set("lset")
        entry = w.suite("syn-short", tick_log(5), launch="lset", outcome=True, exit_state="exited", exit_code=3,
                        outputs={"results.dat": b"RESULTS", "mission.var": b"MISSION-VAR-AFTER"})
        # 3 ticks instead of 5, no return files, exit code 0 instead of 3.
        ctx = w.context("identical", None, STUB_LOG=self.log_file(tick_log(3)), STUB_EXIT=0)
        result = self.run_one(entry, ctx)
        self.assertEqual(result["verdict"], "DIFFERENT")
        kinds = {p["kind"] for p in result["report"]["problems"]}
        self.assertEqual(kinds, {"tick_frames", "outputs", "exit_code"})
        self.assertIn("first difference tick 3 field tick_frames", result["line"])
        # The earliest difference among kinds without a frame is the frame one; with equal ticks it is the exit code.
        frame, field, text = rs.first_difference({"problems": [{"kind": "exit_code", "golden": 3, "run": 0}]})
        self.assertEqual((frame, field), (None, "exit code"))

    def test_golden_stopped_by_the_runner_does_not_compare_the_exit_code(self):
        w = self.world
        w.launch_set(rs.LEGACY_LAUNCH)
        entry = w.suite("syn-none", tick_log(4), exit_state="none")
        ctx = w.context("identical", None, STUB_LOG=self.log_file(tick_log(4)), STUB_EXIT=0)
        self.assertEqual(self.run_one(entry, ctx)["verdict"], "IDENTICAL")

    def test_refused_names_the_call(self):
        w = self.world
        w.launch_set(rs.LEGACY_LAUNCH)
        entry = w.suite("syn-refused", tick_log(5))
        result = self.run_one(entry, w.context("refuse"))
        self.assertEqual(result["verdict"], "REFUSED")
        for text in ("REFUSED 0x0049630c", '"frame_record"', "no construction counter", "called from 0x0045f4ad",
                     "docs/functions/dbsim-490000.md", "[2 ticks"):
            self.assertIn(text, result["line"])
        self.assertEqual(result["refusal"]["va"], "0x0049630c")

    def test_fault_setup_failure_and_nothing_run(self):
        w = self.world
        w.launch_set(rs.LEGACY_LAUNCH)
        entry = w.suite("syn-misc", tick_log(5), exit_state="none")
        fault = self.run_one(entry, w.context("fault"))
        self.assertEqual(fault["verdict"], "FAULT")
        self.assertIn("0xc0000005", fault["line"])
        setup = self.run_one(entry, w.context("setup"))
        self.assertEqual(setup["verdict"], "SETUP-FAILED")
        self.assertIn("host exit 65: es2sim: image refused: size gate", setup["line"])
        nothing = self.run_one(entry, w.context("nothing"))
        self.assertEqual(nothing["verdict"], "NOT-RUN")
        self.assertIn("exited with 0 (golden: 5 ticks, exit code none)", nothing["line"])

    def test_guest_fault_is_the_originals_fault_path(self):
        w = self.world
        w.launch_set(rs.LEGACY_LAUNCH)
        # D: without a golden that ended the same way the verdict is FAULT with the host's line, not a difference.
        entry = w.suite("syn-gfault", tick_log(4), exit_state="none")
        result = self.run_one(entry, w.context("guest_fault", None, STUB_LOG=self.log_file(tick_log(4))))
        self.assertEqual(result["verdict"], "FAULT")
        for text in ("FAULT FAULT: guest exception 0xc0000094 at 0x0047de3c", "(golden: exit code none)", "[4 ticks"):
            self.assertIn(text, result["line"])
        # A golden whose original exited with the same code is compared as usual.
        entry = w.suite("syn-gfault-same", tick_log(4), exit_state="exited", exit_code=0xC0000094)
        result = self.run_one(entry, w.context("guest_fault", None, STUB_LOG=self.log_file(tick_log(4))))
        self.assertEqual(result["verdict"], "IDENTICAL")
        self.assertIn("exit code 3221225620 (compared)", result["line"])

    def test_timeout(self):
        w = self.world
        w.launch_set(rs.LEGACY_LAUNCH)
        entry = w.suite("syn-hang", tick_log(5))
        result = self.run_one(entry, w.context("hang", timeout=1))
        self.assertEqual(result["verdict"], "TIMEOUT")
        self.assertFalse([d for d in os.listdir(self.root) if d.startswith("replay-syn-hang")], "work directory deleted")

    def test_skips_and_errors(self):
        w = self.world
        entry = w.suite("syn-nogolden", None)
        result = self.run_one(entry, w.context("nothing"))
        self.assertEqual(result["verdict"], "SKIP")
        self.assertIn("no golden", result["line"])
        entry = w.suite("syn-nolaunch", tick_log(2))
        result = self.run_one(entry, w.context("nothing"))
        self.assertEqual(result["verdict"], "SKIP")
        self.assertIn("launch set %s is not under" % rs.LEGACY_LAUNCH, result["line"])
        # A host that cannot be started is an error line, not a traceback.
        broken = w.context("nothing")
        broken["sim"] = [os.path.join(self.root, "no-such-es2sim.exe")]
        w.launch_set(rs.LEGACY_LAUNCH)
        result = self.run_one(entry, broken)
        self.assertEqual(result["verdict"], "ERROR")
        self.assertIn("cannot start", result["line"])
        shutil.rmtree(os.path.join(w.oracle, "launch", rs.LEGACY_LAUNCH))
        # A launch set whose member was changed after its manifest was written is an error, not a run.
        w.launch_set(rs.LEGACY_LAUNCH)
        write(os.path.join(w.oracle, "launch", rs.LEGACY_LAUNCH, "script.dat"), b"changed")
        result = self.run_one(entry, w.context("nothing"))
        self.assertEqual(result["verdict"], "ERROR")
        self.assertIn("size or hash mismatch for script.dat", result["line"])

    def test_locating_inputs(self):
        root = self.root
        self.assertEqual(rs.find_cd(root), os.path.join(root, "extracted", "cd"))
        self.assertEqual(rs.find_cd(os.path.join(root, "extracted", "cd")), os.path.join(root, "extracted", "cd"))
        self.assertIsNone(rs.find_cd(os.path.join(root, "nowhere")))
        self.assertIsNone(rs.find_cd(""))
        self.assertEqual(rs.find_oracle_dir(os.path.join(root, "extracted", "cd")), os.path.join(root, "oracle"))
        self.assertEqual(rs.find_oracle_dir(root), os.path.join(root, "oracle"))
        self.assertEqual(rs.find_oracle_dir(root, os.path.join(root, "elsewhere")), os.path.join(root, "elsewhere"))
        self.assertEqual(rs.find_oracle_dir(os.path.join(root, "extracted", "cd"), None, os.path.join(root, "data")),
                         os.path.join(root, "data", "oracle"))
        self.assertEqual(rs.find_oracle_dir(root, os.path.join(root, "elsewhere"), os.path.join(root, "data")),
                         os.path.join(root, "elsewhere"))

    def test_main_skips_without_data_and_lists(self):
        w = self.world
        w.suite("syn-one", None)
        registry = os.path.join(w.tools, "oracle-suites.json")
        saved = {k: os.environ.pop(k, None) for k in ("ES2_DATA_ROOT", "ES2_DATA_REPO")}
        try:
            self.assertEqual(rs.main(["--list", "--registry", registry]), 0)
            self.assertEqual(rs.main(["--sim", "x", "--suite", "syn-one", "--registry", registry, "--oracle-script", ORACLE_SCRIPT]), 0)
            os.environ["ES2_DATA_ROOT"] = w.cd
            self.assertEqual(rs.main(["--sim", "x", "--suite", "syn-one", "--registry", registry, "--oracle-script", ORACLE_SCRIPT,
                                      "--output-dir", os.path.join(self.root, "o")]), 0)    # no golden: SKIP
            with self.assertRaises(SystemExit):
                rs.main(["--sim", "x", "--suite", "not-registered", "--registry", registry])
        finally:
            os.environ.pop("ES2_DATA_ROOT", None)
            for k, v in saved.items():
                if v is not None:
                    os.environ[k] = v

    def test_sim_argv_pass_through(self):
        # Every argument after --sim-argv is the host's command prefix, unchanged (spaces, an empty argument, option
        # names of the driver, an unexpanded ${NAME}); the driver's own arguments follow it as they follow --sim.
        w = self.world
        w.launch_set(rs.LEGACY_LAUNCH)
        w.suite("syn-argv", tick_log(3), frame_limit=3)
        registry = os.path.join(w.tools, "oracle-suites.json")
        record = os.path.join(self.root, "record.json")
        w.context("identical", record, STUB_LOG=self.log_file(tick_log(3)))
        extra = ["--odd arg", "C:\\x y\\host.mjs", "", "${TEST_TMPDIR}", "--suite", "--sim", "--sim-argv"]
        prefix = [sys.executable, "-B", STUB] + extra
        self.assertEqual(rs.split_sim_argv(["--suite", "s", "--sim-argv"] + prefix), (["--suite", "s"], prefix))
        self.assertEqual(rs.split_sim_argv(["--sim", "x"]), (["--sim", "x"], None))
        saved = {k: os.environ.pop(k, None) for k in ("ES2_DATA_ROOT", "ES2_DATA_REPO", "ES2_ORACLE_DIR")}
        try:
            os.environ["ES2_DATA_ROOT"] = w.cd
            os.environ["ES2_ORACLE_DIR"] = w.oracle
            driver = ["--suite", "syn-argv", "--registry", registry, "--oracle-script", ORACLE_SCRIPT,
                      "--output-dir", os.path.join(self.root, "o")]
            self.assertEqual(rs.main(driver + ["--sim-argv"] + prefix), 0)
            with open(record, encoding="utf-8") as stream:
                argv = json.load(stream)["argv"]
            self.assertEqual(argv[:len(extra)], extra)
            self.assertEqual(argv[len(extra):len(extra) + 2], ["--headless", "--config"])
            # --sim-argv takes the place of --sim and is not empty
            with self.assertRaises(SystemExit):
                rs.main(["--sim", "x"] + driver + ["--sim-argv"] + prefix)
            with self.assertRaises(SystemExit):
                rs.main(driver + ["--sim-argv"])
        finally:
            for k in ("ES2_DATA_ROOT", "ES2_ORACLE_DIR"):
                os.environ.pop(k, None)
            for k, v in saved.items():
                if v is not None:
                    os.environ[k] = v


class FrameEnvironment(unittest.TestCase):
    """Under --frames es2sim's environment copy has the frame references' string count and size."""

    def test_block_sizes(self):
        # CW3220's _malloc(length + 1): rounded up to 4, 8 bytes of header and footer, at least 16.
        self.assertEqual([rs.environment_block(n) for n in (0, 7, 8, 11, 12, 100)], [16, 16, 20, 20, 24, 112])

    def test_kept_variables_and_pads_reach_the_reference(self):
        base = {"SystemRoot": "C:\\Windows", "PATH": "C:\\bin;D:\\tools", "TEMP": "C:\\t", "ES2SIM_PERF": "p.json",
                "SDL_VIDEODRIVER": "dummy", "FOO": "1", "TEST_TMPDIR": "C:\\x"}
        env = rs.frame_environment(base, 102, 5648)
        self.assertEqual(len(env), 102)
        self.assertEqual(rs.environment_copy_size(env), 5648)
        kept = {k: v for k, v in env.items() if not re.fullmatch(r"Z\d{3}", k)}
        self.assertEqual(kept, {k: base[k] for k in ("SystemRoot", "PATH", "TEMP", "ES2SIM_PERF", "SDL_VIDEODRIVER")})
        self.assertEqual(sorted(k for k in env if k not in kept), ["Z%03d" % i for i in range(97)])
        self.assertEqual(rs.frame_environment({"PATH": "x"}, 1, rs.environment_block(len("PATH=x"))), {"PATH": "x"})

    def test_refused_when_the_kept_variables_do_not_fit(self):
        for base, strings, size in (({"PATH": "x" * 6000}, 102, 5648), ({"PATH": "x", "TMP": "y"}, 1, 5648),
                                    ({"PATH": "x"}, 1, 5648)):
            with self.assertRaisesRegex(RuntimeError, "does not fit"):
                rs.frame_environment(base, strings, size)

    def test_reference_fits_this_environment(self):
        strings, size = rs.load_frame_environment()
        env = rs.frame_environment(os.environ, strings, size)
        self.assertEqual((len(env), rs.environment_copy_size(env)), (strings, size))


class LegacyLaunchSet(unittest.TestCase):
    """legacy_launch_set.py writes a set the driver accepts, and refuses a clone that is not in the legacy state."""

    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="legacy-test-")
        self.source = os.path.join(self.root, "DATA")
        self.contents = {"script.dat": b"s", "player.mec": b"p", "mission.var": b"m", "MISSION.STR": b"t"}
        self.expected = {}
        for name, data in self.contents.items():
            write(os.path.join(self.source, name), data)
            import hashlib
            self.expected[name] = hashlib.sha256(data).hexdigest()

    def tearDown(self):
        shutil.rmtree(self.root, ignore_errors=True)

    def test_written_set_is_read_by_the_driver(self):
        destination = os.path.join(self.root, "oracle", "launch", rs.LEGACY_LAUNCH)
        legacy_launch_set.build(self.source, destination, expected=self.expected)
        directory, hashes = rs.read_launch_set(os.path.join(self.root, "oracle"), rs.LEGACY_LAUNCH)
        self.assertEqual(hashes, self.expected)
        self.assertEqual(sorted(os.listdir(directory)), sorted(list(self.contents) + ["manifest.json"]))
        with self.assertRaises(SystemExit):     # immutable
            legacy_launch_set.build(self.source, destination, expected=self.expected)

    def test_refuses_another_state_and_dry_run_writes_nothing(self):
        write(os.path.join(self.source, "script.dat"), b"other")
        destination = os.path.join(self.root, "launch", "x")
        with self.assertRaises(SystemExit) as raised:
            legacy_launch_set.build(self.source, destination, expected=self.expected)
        self.assertIn("script.dat has SHA-256", str(raised.exception))
        self.assertFalse(os.path.exists(destination))
        write(os.path.join(self.source, "script.dat"), b"s")
        legacy_launch_set.build(self.source, destination, dry_run=True, expected=self.expected)
        self.assertFalse(os.path.exists(destination))

    def test_pinned_hashes_cover_the_four_launch_members(self):
        self.assertEqual(sorted(legacy_launch_set.MEMBERS), sorted(rs.LAUNCH_FILES))
        self.assertEqual(legacy_launch_set.NAME, rs.LEGACY_LAUNCH)


class TapePlayback(unittest.TestCase):
    def test_demo_list_has_one_entry(self):
        self.assertEqual(rs.demo_list("DEMO1"), b"\x0b\x00\x00\x00\x01\x00\x06\x00DEMO1\x00\x00")

    def test_playback_arguments(self):
        self.assertTrue(rs.playback({"game_arguments": ["-ptapes\\demo2"]}))
        self.assertTrue(rs.playback({"game_arguments": ["/D"]}))
        self.assertFalse(rs.playback({"game_arguments": ["-rtapes\\x"]}))
        self.assertFalse(rs.playback({"game_arguments": []}))


class RegisteredSuites(unittest.TestCase):
    """The real registry: every suite has a plan, an INI the host can read and a Bazel target."""

    def test_every_registered_suite_plans(self):
        suites = rs.load_registry(REGISTRY)
        self.assertGreaterEqual(len(suites), 40)
        tool_root = os.path.dirname(REGISTRY)
        watch = re.compile(r"^[A-Za-z0-9_]+ 0x[0-9a-fA-F]+(@0x[0-9a-fA-F]+|@[0-9]+)* (w0x[0-9a-fA-F]+\*)?[0-9]+$")
        for name, suite in suites.items():
            plan = rs.build_plan(name, suite, tool_root)
            self.assertTrue(os.path.isfile(plan["script"]), name)
            self.assertTrue(plan["watches"] and len(plan["watches"]) <= 48, name)
            for line in plan["watches"]:
                self.assertRegex(line, watch, name)
            text = rs.config_text(plan)
            self.assertIn("virtual_time_step_ms=47", text)
            self.assertEqual("frame_limit" in text,
                             suite.get("frame_limit") is not None or bool(suite.get("hold")), name)
            self.assertEqual("[game]" in text, bool(suite.get("game_arguments")), name)
            self.assertEqual(plan["launch"], suite.get("launch") or rs.LEGACY_LAUNCH)
            # [snapshot] only when asked, last, with extra only when given.
            self.assertNotIn("[snapshot]", text, name)
            self.assertEqual(rs.config_text(plan, snapshot=("C:\\s.snap", "")),
                             text + "[snapshot]\r\nafter_load=1\r\npath=C:\\s.snap\r\n", name)
            self.assertEqual(rs.config_text(plan, snapshot=("C:\\s.snap", "0x6cbd58:0x18c")),
                             text + "[snapshot]\r\nafter_load=1\r\npath=C:\\s.snap\r\nextra=0x6cbd58:0x18c\r\n", name)

    def test_one_bazel_target_per_registered_suite(self):
        with open(SUITES_BZL, encoding="utf-8") as stream:
            block = re.search(r"^SUITES = \[(.*?)^\]", stream.read(), re.M | re.S).group(1)
        listed = re.findall(r'"([A-Za-z0-9_-]+)"', block)
        self.assertEqual(listed, list(rs.load_registry(REGISTRY)), "sim/replay_tests.bzl must list the registry's suites in order")


if __name__ == "__main__":
    unittest.main(verbosity=1)
