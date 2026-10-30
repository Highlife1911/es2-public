# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""The proxy's final exit_counters record is never compared, and its
hk_* counters reach the normalized run as `exit_counters`."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

HOOKS = ("rng_next", "damage_score", "result_conditions")


def counters(scale, late=0):
    """Fields in the proxy's order: calls, bypassed, then callee counters."""
    fields = {}
    for i, hook in enumerate(HOOKS):
        fields[f"hk_{hook}_calls"] = scale * (i + 1) + (late if hook != "rng_next" else 0)
        fields[f"hk_{hook}_bypassed"] = 0
    fields["hk_result_conditions_call_41b4ec"] = 5 * late
    fields["hk_result_conditions_slot_14"] = late
    return fields


def rtl_counters(scale):
    """The CW3220 import shim's full exit fields (rtl_imports.md)."""
    return {"rtl_memset_calls": 9 * scale, "rtl_memset_mismatches": 0, "rtl_memset_hook": 4 * scale,
            "rtl_memset_sim": 5 * scale, "rtl_memset_dbsim": 0, "rtl_memset_other": 0, "rtl_foreign_calls": 0}


def main():
    oracle_path, comparison_path = map(Path, sys.argv[1:])
    spec = importlib.util.spec_from_file_location("oracle", oracle_path)
    oracle = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(oracle)
    start = dict(event="start", schema=2, record_limit=1000000, actor_raw=False, watches=1)
    ticks = []
    for frame in range(4):
        ticks.append(dict(event="tick", sequence=frame + 1, qpc=frame * 100000, frame_counter_before_increment=frame,
                          completed_clock_ms=frame * 47, loop_pause_flag=0, limiter_bypassed=False,
                          clock_raw_hex="00", rng_a_hex="%02x" % frame, rng_b_hex="01",
                          position_raw_i32=[1, 2, 3], angles_raw_u16=[4, 5, 6], speed_raw_i16=7,
                          throttle_raw_q10=8, **counters(frame)))
    watch = dict(event="watch", sequence=2, frame=1, name="bank", address="0x1000", length=2, hex="abcd")
    exit_fields = dict(counters(3, late=1), **rtl_counters(3))
    exit_record = dict(event="exit_counters", reason="shutdown", last_sequence=4, frame_counter=4, **exit_fields)

    def write(path, rows):
        # Bytes as the proxy writes them (CRLF), without text-mode translation.
        path.write_bytes("".join(json.dumps(row, separators=(",", ":")) + "\r\n" for row in rows).encode("utf-8"))

    with tempfile.TemporaryDirectory(dir=os.environ["TEST_TMPDIR"]) as tmp:
        tmp = Path(tmp)
        plain, exited, other = tmp / "plain.jsonl", tmp / "exited.jsonl", tmp / "other.jsonl"
        write(plain, [start, ticks[0], ticks[1], watch, ticks[2], ticks[3]])
        write(exited, [start, ticks[0], ticks[1], watch, ticks[2], ticks[3], exit_record])
        different = dict(exit_record, reason="runner_flush", hk_damage_score_calls=999999)
        # A runner flush may be followed by ticks logged before the stop.
        write(other, [start, ticks[0], ticks[1], watch, ticks[2], different, ticks[3]])

        before, after, moved = oracle.normalize(plain), oracle.normalize(exited), oracle.normalize(other)
        # Ignored by the comparison: identical ticks, watches and outputs.
        assert "exit_counters" not in before and "exit_record" not in before, before.keys()
        assert {k: v for k, v in after.items() if not k.startswith("exit_")} == before
        assert {k: v for k, v in moved.items() if not k.startswith("exit_")} == before
        for golden, run in ((before, after), (after, before), (after, moved), (moved, after)):
            report = oracle.compare(golden, run)
            assert report["identical"] and not report["problems"], report
        # Copied into the run: every counter, in the record's order.
        assert list(after["exit_counters"].items()) == list(exit_fields.items())
        assert after["exit_record"] == dict(reason="shutdown", last_sequence=4, frame_counter=4)
        assert after["exit_counters"]["hk_damage_score_calls"] == 7
        assert after["exit_counters"]["hk_result_conditions_call_41b4ec"] == 5
        assert after["exit_counters"]["rtl_memset_sim"] == 15 and after["exit_counters"]["rtl_foreign_calls"] == 0
        assert moved["exit_counters"]["hk_damage_score_calls"] == 999999 and moved["exit_record"]["reason"] == "runner_flush"
        # The exit snapshot does not depend on the comparison window.
        limited = oracle.normalize(exited, frame_limit=2)
        assert limited["tick_count"] == 2 and limited["exit_counters"] == after["exit_counters"]
        # A changed tick is still detected with the record present.
        changed_tick = dict(ticks[2], completed_clock_ms=1)
        changed = tmp / "changed.jsonl"
        write(changed, [start, ticks[0], ticks[1], watch, changed_tick, ticks[3], exit_record])
        assert [p["kind"] for p in oracle.compare(after, oracle.normalize(changed))["problems"]] == ["tick_field"]

        # Command line as run-oracle-regression.ps1 uses it.
        def cli(*args):
            return subprocess.run([sys.executable, "-B", str(oracle_path), *map(str, args)],
                                  capture_output=True, text=True)
        plain_out, exited_out = tmp / "plain.json", tmp / "exited.json"
        first, second = cli("normalize", plain, plain_out), cli("normalize", exited, exited_out)
        assert first.returncode == 0 and second.returncode == 0, (first.stderr, second.stderr)
        summary = json.loads(second.stdout)
        assert summary["exit_counter_fields"] == len(exit_fields) and "exit_counter_fields" not in json.loads(first.stdout)
        stored = json.loads(exited_out.read_text(encoding="utf-8"))
        assert stored["exit_counters"] == exit_fields and stored["exit_record"]["reason"] == "shutdown"
        # A log without the record keeps the previous normalized bytes.
        assert plain_out.read_text(encoding="utf-8") == json.dumps(before, separators=(",", ":"))
        for golden, run in ((plain_out, exited_out), (exited_out, plain_out)):
            result = cli("compare", golden, run, "--report", tmp / "report.json")
            assert result.returncode == 0, result.stdout
            assert json.loads((tmp / "report.json").read_text(encoding="utf-8"))["identical"]
        # The legacy two-log tool reads only tick events.
        output = subprocess.check_output([sys.executable, "-B", str(comparison_path), str(plain), str(exited)], text=True)
        assert "n = 4" in output and output.count("differs at    0 counters") == 8, output
    print(f"PASS: exit_counters ({len(exit_fields)} fields) ignored by compare in both directions, "
          "copied into the normalized run and the CLI summary; ticks unchanged")


if __name__ == "__main__":
    main()
