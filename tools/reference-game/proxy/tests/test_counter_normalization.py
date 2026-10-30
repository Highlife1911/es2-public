# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""The production normalizers must ignore old and new diagnostic fields."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    oracle_path, comparison_path = map(Path, sys.argv[1:])
    spec = importlib.util.spec_from_file_location("oracle", oracle_path)
    oracle = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(oracle)
    update = "402628 418f40 46e180 418f74 4027fc 492ee0 462830 4792ac 4195c8".split()
    direct = "404ae4 46e498 492f24 434010 462830".split()
    slots = "20 5c 68 70 74 7c".split()
    fields = (["hk_ground_update_call_" + x for x in update]
              + ["hk_ground_collide_call_" + x for x in direct]
              + ["hk_ground_collide_slot_" + x for x in slots]
              + ["hk_ground_collide_path_" + x for x in ("contact", "slope_drift", "slide_damage")]
              + ["hk_rng_next_calls", "hk_ground_update_calls", "hk_ground_collide_calls"]
              # The CW3220 import shim's per-tick counters (rtl_imports.md).
              + ["rtl_memset_calls", "rtl_strcat_calls", "rtl_foreign_calls"])
    ticks = []
    for frame in range(3):
        ticks.append(dict(event="tick", frame_counter_before_increment=frame, qpc=frame * 100000,
                          completed_clock_ms=frame * 40, loop_pause_flag=0, limiter_bypassed=False,
                          clock_raw_hex="00", rng_a_hex="00", rng_b_hex="01",
                          position_raw_i32=[1, 2, 3], angles_raw_u16=[4, 5, 6],
                          speed_raw_i16=7, throttle_raw_q10=8))
    def write(path, rows):
        path.write_text("".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8")
    with tempfile.TemporaryDirectory(dir=os.environ["TEST_TMPDIR"]) as tmp:
        clean, counted, changed = (Path(tmp) / (x + ".jsonl") for x in ("clean", "counted", "changed"))
        write(clean, ticks)
        write(counted, [dict(t, **{f: (2**64 - 1 - i) for i, f in enumerate(fields)}) for t in ticks])
        before, after = oracle.normalize(clean), oracle.normalize(counted)
        assert before == after
        assert not oracle.compare(before, after)["problems"]
        output = subprocess.check_output([sys.executable, "-B", str(comparison_path), str(clean), str(counted)], text=True)
        differences = [line for line in output.splitlines() if " differs at " in line]
        assert len(differences) == 8 and all("differs at    0 counters" in x for x in differences), output
        rows = [dict(t) for t in ticks]
        rows[1]["completed_clock_ms"] += 1
        write(changed, rows)
        assert oracle.compare(before, oracle.normalize(changed))["problems"]
        output = subprocess.check_output([sys.executable, "-B", str(comparison_path), str(clean), str(changed)], text=True)
        assert "completed_clock_ms   differs at    1 counters" in output, output
    print(f"PASS: both production oracle tools ignore all {len(fields)} diagnostic fields; clock change still detected")


if __name__ == "__main__":
    main()
