"""configure-sim.ps1 (the own DBSIM process es2sim as what our launcher starts, and back) on a synthetic
installation.

The installation lives in a temporary DataRoot and holds stand-in files of our own (no game file); its
installation-manifest.json records their hashes as the "originals". The deployed es2sim.exe and SDL3.dll are stand-in
32-bit PE images written by the test. With the real es2launch.exe (argument 2) one case reads the deployed
es2launch.ini in a dry run (ES2_LAUNCH_SCRIPT: nothing is started) and checks the es2sim command line it would start.
No game, no es2sim run, no real installation or clone is touched.
Usage: test_configure_sim.py <configure-sim.ps1> [<es2launch.exe>]
"""
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

SCRIPT = Path(sys.argv[1]).resolve()
LAUNCHER = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None
SETUP = SCRIPT.parent
PWSH = os.environ.get("ES2_PWSH") or shutil.which("pwsh")
ORIGINALS = ["ES.EXE", "VSHELL.EXE", "DBSIM.EXE", "CW3220.DLL", "BWCC32.DLL", "SOS9503.DLL", "SOSLIBS3.DLL"]
NO_WINDOW = 0x08000000
sys.path.insert(0, str(SETUP))
from pwsh_session import PwshSession  # noqa: E402
SESSION = None
checks = 0


def check(value, detail=""):
    global checks
    checks += 1
    if not value:
        raise AssertionError(detail)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest().upper()


def pe32(tag):
    """A minimal PE32 image header (MZ, e_lfanew, PE\\0\\0, machine i386) plus a tag."""
    header = bytearray(b"MZ" + b"\0" * 0x3a + struct.pack("<I", 0x40))
    header += b"PE\0\0" + struct.pack("<H", 0x14c) + b"\0" * 18
    header += tag.encode() + b"\0" * (0x100 - len(header) - len(tag))
    return bytes(header)


def error_str():
    """A synthetic error.str (u32 body size; group 0 with three messages, group 1 with a caption)."""
    body = bytearray(struct.pack("<H", 3))
    for text in (b"memory 0", b"memory 1", b"memory 2"):
        body += struct.pack("<H", len(text) + 1) + text + b"\0\0"
    body += struct.pack("<H", 1) + struct.pack("<H", 8) + b"caption\0\0"
    return struct.pack("<I", len(body)) + bytes(body)


def run(*args, ok=True, env=None):
    result = SESSION.run(file=SCRIPT, args=[str(a) for a in args], env=env, timeout=180)
    if (result.returncode == 0) != ok:
        print(json.dumps(dict(args=[str(a) for a in args], code=result.returncode, out=result.stdout, err=result.stderr)), flush=True)
    check((result.returncode == 0) == ok, (args, result.stdout, result.stderr))
    return result


def report(result):
    text = result.stdout
    return json.loads(text[text.index("{"):])


def refused(result, text):
    check(text in result.stderr + result.stdout, (text, result.stderr, result.stdout))


def main():
    if not PWSH:
        print("SKIP: no PowerShell 7 (ES2_PWSH)")
        return 0
    base = Path(os.environ.get("TEST_TMPDIR") or tempfile.gettempdir())
    root = Path(tempfile.mkdtemp(prefix="ownsim-", dir=base))
    global SESSION
    SESSION = PwshSession(PWSH, scratch=root)
    data = root / "DataRoot"
    install = data / "installs" / "own-sim-mock"
    (install / "DATA").mkdir(parents=True)
    original = {}
    for name in ORIGINALS:
        (install / name).write_bytes(("stand-in for " + name + "\n").encode() * 50)
        original[name] = sha(install / name)
    (install / "error.str").write_bytes(error_str())
    destination = os.path.abspath(str(install))
    manifest = dict(destination=destination, measurement_clone_of="X:\\source", hashes=[dict(file=n, sha256=v) for n, v in original.items()])
    (install / "installation-manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    sim, sdl, config = root / "es2sim.bin", root / "sdl.bin", root / "suite.ini"
    sim.write_bytes(pe32("es2sim"))
    sdl.write_bytes(pe32("sdl3"))
    config.write_bytes("[time]\r\nvirtual_time_step_ms=47\r\n".encode("utf-16"))
    tick_log, output = root / "run" / "suite.jsonl", root / "run" / "es2sim-output.txt"
    common = ["-DataRoot", data, "-InstallName", "own-sim-mock"]
    full = ["-Apply", "-Sim", sim, "-Sdl", sdl, "-Config", config, "-Headless", "-TickLog", tick_log, "-Output", output]
    sim_dir, launch_ini, manifest_path = install / "ES2SIM", install / "es2launch.ini", install / "own-sim-manifest.json"

    # Preview: nothing deployed, nothing changed; deployment options belong to -Apply.
    plan = report(run(*common))
    check(plan["state"] == "none" and plan["preview"] is True, plan)
    check(not sim_dir.exists() and not launch_ini.exists() and not manifest_path.exists())
    refused(run(*common, "-Headless", ok=False), "belong to -Apply")

    # Apply: ES2SIM\ with the three files, es2launch.ini with the command and the output, the manifest.
    applied = report(run(*common, *full))
    check(applied["applied"] is True and applied["state"] == "own", applied)
    for key, path, source in (("sim", sim_dir / "es2sim.exe", sim), ("sdl", sim_dir / "SDL3.dll", sdl), ("config", sim_dir / "es2sim.ini", config)):
        check(path.read_bytes() == source.read_bytes() and applied["files"][key] == sha(source), key)
    # The command ends with a quote, so the INI value has one more quote pair.
    command = f'"{sim_dir / "es2sim.exe"}" --headless --config "{sim_dir / "es2sim.ini"}" --tick-log "{tick_log}"'
    ini = launch_ini.read_bytes().decode("ascii")
    check(ini.endswith(f"[sim]\r\ncommand = \"{command}\"\r\noutput = {output}\r\n") and ini.startswith("; H5-27"), ini)
    check(applied["command"] == command and applied["files"]["launch_ini"] == sha(launch_ini), applied)
    record = json.loads(manifest_path.read_text(encoding="utf-8"))
    check(record["kind"] == "es2-own-sim" and record["files"] == applied["files"] and record["command"] == command and
          "dbsim_sha256" not in record and record["output"] == str(output), record)
    check(not (install / "ES2SIM.staged").exists() and not (install / "es2launch.ini.staged").exists())
    plan = report(run(*common))
    check(plan["state"] == "own" and plan["command"] == command, plan)
    # The same deployment again changes nothing.
    check(report(run(*common, *full))["applied"] is False)

    # The real launcher reads the deployed es2launch.ini (dry run: nothing is started).
    if LAUNCHER:
        log = root / "launch.log"
        env = dict(os.environ, ES2_LAUNCH_SCRIPT="2,3,0", ES2_LAUNCH_LOG=str(log), ES2_LAUNCH_NO_UI="1")
        for name in ("ES2_LAUNCH_SIM", "ES2_LAUNCH_SIM_OUTPUT", "ES2_LAUNCH_SHELL"):
            env.pop(name, None)
        completed = subprocess.run([str(LAUNCHER)], cwd=str(install), env=env, timeout=60, creationflags=NO_WINDOW)
        check(completed.returncode == 0, completed.returncode)
        text = log.read_text(encoding="ascii")
        check(f'start sim-replacement {command} --arguments "-eggplant -X2 -R0"\n' in text and
              "exit sim-replacement 3 (scripted)" in text and "start vshell.EXE dummy -eggplant -X3" in text and
              " end 0" in text and "dbsim.EXE" not in text, text)
        log.unlink()

    # A changed deployed file: -Apply refuses, -Remove clears it.
    saved = (sim_dir / "es2sim.exe").read_bytes()
    (sim_dir / "es2sim.exe").write_bytes(saved + b"x")
    refused(run(*common, *full, ok=False), "Unexpected own-sim files")
    (sim_dir / "es2sim.exe").write_bytes(saved)

    # A new build and another command replace the deployment (no config: es2sim.ini goes, no output line).
    newer = root / "es2sim2.bin"
    newer.write_bytes(pe32("es2sim-2"))
    updated = report(run(*common, "-Apply", "-Sim", newer, "-Sdl", sdl))
    check(updated["applied"] is True and (sim_dir / "es2sim.exe").read_bytes() == newer.read_bytes(), updated)
    check(not (sim_dir / "es2sim.ini").exists() and "config" not in updated["files"], updated)
    check(launch_ini.read_bytes().decode("ascii").endswith(f'command = ""{sim_dir / "es2sim.exe"}""\r\n'))
    check(json.loads(manifest_path.read_text(encoding="utf-8"))["files"]["sim"] == sha(newer))

    # Refused sources and paths; nothing changed.
    before = {p.name: sha(p) for p in sim_dir.iterdir()}
    bad = root / "bad.bin"
    bad.write_bytes(b"not an image" * 40)
    refused(run(*common, "-Apply", "-Sim", bad, "-Sdl", sdl, ok=False), "Not a 32-bit PE image")
    refused(run(*common, "-Apply", "-Sim", sim, "-Sdl", sdl, "-Config", install / "DBSIM.EXE", ok=False), "original file")
    refused(run(*common, "-Apply", "-Sim", sim, "-Sdl", sdl, "-TickLog", root / "t\u00e4ck.jsonl", ok=False), "plain ASCII")
    refused(run(*common, "-Apply", "-Sim", sim, "-Sdl", sdl, "-Config", root / "missing.ini", ok=False), "No es2sim configuration")
    # es2sim reads no DBSIM.EXE, so a file without the manifest's hash does not matter to the deployment.
    dbsim = install / "DBSIM.EXE"
    kept = dbsim.read_bytes()
    dbsim.write_bytes(kept + b"patched")
    check(report(run(*common, "-Apply", "-Sim", newer, "-Sdl", sdl))["applied"] is False)
    dbsim.write_bytes(kept)
    check({p.name: sha(p) for p in sim_dir.iterdir()} == before)

    # Another session's clone lock refuses -Apply and -Remove (not the preview).
    locks = data / "installs" / ".clone-locks"
    locks.mkdir(parents=True, exist_ok=True)
    lock = locks / "own-sim-mock.json"
    lock.write_text(json.dumps(dict(token="other", holder_pid=1, session="another")), encoding="utf-8")
    with open(lock, "rb"):
        refused(run(*common, "-Remove", ok=False), "in use by another session")
        refused(run(*common, *full, ok=False), "in use by another session")
        check(report(run(*common))["state"] == "own")
    lock.unlink()

    # Remove: everything of the own simulator gone, the originals untouched.
    (sim_dir / "es2sim.log").write_bytes(b"a run")
    removed = report(run(*common, "-Remove"))
    check(removed["removed"] is True and removed["state"] == "none", removed)
    check(not sim_dir.exists() and not launch_ini.exists() and not manifest_path.exists())
    check(report(run(*common, "-Remove"))["removed"] is False)
    # A stray es2launch.ini without a manifest is unknown: -Apply refuses, -Remove clears.
    launch_ini.write_bytes(b"[sim]\r\ncommand = x\r\n")
    refused(run(*common, *full, ok=False), "Unexpected own-sim files")
    check(report(run(*common, "-Remove"))["removed"] is True and not launch_ini.exists())

    # Refused installations and arguments.
    refused(run("-DataRoot", data, "-InstallName", "native-r111", ok=False), "owner's word")
    primary = data / "installs" / "native-r111"
    shutil.copytree(install, primary)
    (primary / "installation-manifest.json").write_text(json.dumps(dict(manifest, destination=os.path.abspath(str(primary)))), encoding="utf-8")
    applied = report(run("-DataRoot", data, "-InstallName", "native-r111", "-AllowPrimary", "-Apply", "-Sim", sim, "-Sdl", sdl))
    check(applied["applied"] is True and (primary / "ES2SIM" / "es2sim.exe").exists(), applied)
    shutil.rmtree(primary)
    refused(run("-DataRoot", data, "-InstallName", "..\\x", ok=False), "plain installation name")
    refused(run(*common, "-Apply", "-Remove", ok=False), "either -Apply or -Remove")
    refused(run("-DataRoot", data, "-InstallName", "nothing-here", ok=False), "Not a prepared installation")
    for name in ORIGINALS:
        check(sha(install / name) == original[name], name)
    SESSION.close()
    shutil.rmtree(root, ignore_errors=True)
    print(f"PASS configure_sim: {checks} checks (preview, apply, launcher dry run {'run' if LAUNCHER else 'skipped'}, "
          "same, update, refused sources, paths, DBSIM hash and lock, remove, stray files, installations)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
