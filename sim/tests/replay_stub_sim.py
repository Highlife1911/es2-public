"""A stand-in for es2sim.exe in the unit test of replay_suite.py: no game, no data.

It takes the arguments the driver gives the real host (`--headless --config INI --tick-log LOG`; no --exe), records
what it saw (arguments, the INI, the files of its working directory) to the file named by STUB_RECORD, and behaves as
STUB_SCENARIO says:

  identical   copies STUB_LOG (a tick log the test wrote) to the tick-log path, optionally writes the return files
              (STUB_OUTPUTS=1), exits with STUB_EXIT (default 0)
  refuse      logs a start line, two ticks and a "refused" event (the host's record), prints it to stderr, exits 70
  fault       prints a FAULT line, exits 71
  guest_fault copies STUB_LOG to the tick-log path, prints the host's "FAULT: guest exception" line, exits 0xC0000094
              (the original's unhandled divide fault)
  setup       prints "es2sim: image refused: ..." to stderr, exits 65
  nothing     logs a start and an exit record only, exits 0
  hang        sleeps (the driver's timeout ends it)
"""
import json
import os
import sys
import time

TICK = ('{"event":"tick","sequence":%d,"qpc":0,"ebx":"0x00000000","frame_counter_before_increment":%d,'
        '"completed_clock_ms":%d,"limiter_bypassed":0,"loop_pause_flag":0,"clock_raw_hex":"00","rng_a_hex":"aa",'
        '"rng_b_hex":"bb","position_raw_i32":[1,2,3],"angles_raw_u16":[0,0,0],"speed_raw_i16":0,"throttle_raw_q10":0,'
        '"vt_limiter_polls":%d,"vt_stall_advances":0,"in_injected":0,"in_fallbacks":0}\r\n')
START = '{"event":"start","schema":2,"mode":"own_process_headless","record_limit":0,"actor_raw":0,"watches":0}\r\n'


def main(argv):
    options = {}
    it = iter(argv[1:])
    for a in it:
        if a == "--headless":
            options["headless"] = True
        elif a in ("--config", "--tick-log"):
            options[a[2:]] = next(it)
    scenario = os.environ.get("STUB_SCENARIO", "identical")
    record = os.environ.get("STUB_RECORD")
    if record:
        listing = sorted(os.path.relpath(os.path.join(d, f), ".").replace("\\", "/")
                         for d, _, files in os.walk(".") for f in files)
        with open(options["config"], "rb") as stream:
            ini = stream.read()
        data = {"argv": argv[1:], "cwd": os.getcwd(), "listing": listing,
                "ini": ini.decode("utf-16"), "ini_bom": ini[:2].hex(), "tick_log_existed": os.path.exists(options["tick-log"]),
                "drive_cfg": open(os.path.join("DATA", "DRIVE.CFG"), "rb").read().decode("ascii"),
                "language_cfg": open(os.path.join("DATA", "LANGUAGE.CFG"), "rb").read().decode("ascii"),
                "mission_var": open(os.path.join("DATA", "mission.var"), "rb").read().hex(),
                # The windowed replay's environment
                "env": {k: os.environ.get(k) for k in ("SDL_VIDEO_DRIVER", "SDL_VIDEODRIVER", "SDL_AUDIO_DRIVER",
                                                       "SDL_AUDIODRIVER", "ES2SIM_MESSAGE_BOX", "ES2SIM_MESSAGE_TRACE")}}
        with open(record, "w", encoding="utf-8") as stream:
            json.dump(data, stream)
    log = options["tick-log"]
    if os.environ.get("STUB_NOTICES"):
        # The windowed host's notice summary line
        print("es2sim: notices: " + os.environ["STUB_NOTICES"], flush=True)
    if scenario == "identical":
        with open(os.environ["STUB_LOG"], "rb") as source, open(log, "xb") as target:
            target.write(source.read())
        if os.environ.get("STUB_OUTPUTS") == "1":
            with open(os.path.join("DATA", "results.dat"), "wb") as stream:
                stream.write(b"RESULTS")
            with open(os.path.join("DATA", "mission.var"), "wb") as stream:
                stream.write(b"MISSION-VAR-AFTER")
        return int(os.environ.get("STUB_EXIT", "0"))
    if scenario == "refuse":
        refusal = ('{"event":"refused","va":"0x0049630c","name":"frame_record","reason":"no construction counter",'
                   '"return":"0x0045f4ad","frame":"0x0019ff00","index":"docs/functions/dbsim-490000.md"}\r\n')
        with open(log, "x", newline="") as stream:
            stream.write(START + TICK % (1, 0, 0, 1) + TICK % (2, 1, 47, 2) + refusal)
        sys.stderr.write(refusal)
        return 70
    if scenario == "fault":
        sys.stderr.write("FAULT: exception 0xc0000005 at 0x00401234\n")
        return 71
    if scenario == "guest_fault":
        with open(os.environ["STUB_LOG"], "rb") as source, open(log, "xb") as target:
            target.write(source.read())
        sys.stderr.write("FAULT: guest exception 0xc0000094 at 0x0047de3c (the original's unhandled fault); "
                         "ExitProcess(0xc0000094) without exit records\n")
        return 0xC0000094 - (1 << 32)   # sys.exit takes a C long: the signed value gives the same exit code
    if scenario == "setup":
        sys.stderr.write("es2sim: image refused: size gate\n")
        return 65
    if scenario == "nothing":
        with open(log, "x", newline="") as stream:
            stream.write(START + '{"event":"exit","reason":"headless_nothing_loaded","exit_code":0,"frames":0,'
                         '"trapped_calls":0,"calls":[]}\r\n')
        return 0
    if scenario == "hang":
        time.sleep(60)
        return 0
    return 99


if __name__ == "__main__":
    sys.exit(main(sys.argv))
