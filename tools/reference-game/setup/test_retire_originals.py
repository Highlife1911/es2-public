"""retire-originals.ps1 (DBSIM.EXE and VSHELL.EXE out of a played installation, and back) on a synthetic
installation, with configure-shell.ps1 and configure-sim.ps1 deploying stand-in PE images first, and start-native.ps1
only in -Preview.

The installation lives in a temporary DataRoot and holds stand-in files of our own (no game file); its
installation-manifest.json records their hashes as the "originals", and a stand-in extracted CD (VER95) is the source
of -Restore. No game, no mount, no real installation or clone is touched.
Usage: test_retire_originals.py <retire-originals.ps1>
"""
import hashlib
import json
import os
import shutil
import struct
import sys
import tempfile
from pathlib import Path

SCRIPT = Path(sys.argv[1]).resolve()
SETUP = SCRIPT.parent
PWSH = os.environ.get("ES2_PWSH") or shutil.which("pwsh")
ORIGINALS = ["ES.EXE", "VSHELL.EXE", "DBSIM.EXE", "CW3220.DLL", "BWCC32.DLL", "SOS9503.DLL", "SOSLIBS3.DLL"]
RETIRED = ["DBSIM.EXE", "VSHELL.EXE"]
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


def run(script, *args, ok=True):
    result = SESSION.run(file=script, args=[str(a) for a in args], timeout=180)
    if (result.returncode == 0) != ok:
        print(json.dumps(dict(script=str(script), args=[str(a) for a in args], code=result.returncode,
                              out=result.stdout, err=result.stderr)), flush=True)
    check((result.returncode == 0) == ok, (script, args, result.stdout, result.stderr))
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
    root = Path(tempfile.mkdtemp(prefix="retire-", dir=base))
    global SESSION
    SESSION = PwshSession(PWSH, scratch=root)
    data = root / "DataRoot"
    install = data / "installs" / "played-mock"
    (install / "DATA").mkdir(parents=True)
    extracted = root / "extracted"
    (extracted / "cd" / "VER95").mkdir(parents=True)
    (extracted / "earthsiege2-data.iso").write_bytes(b"stand-in, never mounted")
    original = {}
    for name in ORIGINALS:
        (install / name).write_bytes(("stand-in for " + name + "\n").encode() * 50)
        original[name] = sha(install / name)
    for name in RETIRED:
        shutil.copyfile(install / name, extracted / "cd" / "VER95" / name)
    destination = os.path.abspath(str(install))
    (install / "DATA" / "DRIVE.CFG").write_bytes(f"Q:\r\n{destination}\r\n".encode("ascii"))
    manifest = dict(destination=destination, language="German", hashes=[dict(file=n, sha256=v) for n, v in original.items()])
    (install / "installation-manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    common = ["-DataRoot", data, "-InstallName", "played-mock"]
    restore = common + ["-ExtractedRoot", extracted, "-Restore"]
    native = common + ["-ExtractedRoot", extracted]
    start_native, shell_script, sim_script = SETUP / "start-native.ps1", SETUP / "configure-shell.ps1", SETUP / "configure-sim.ps1"
    record = install / "retired-originals.json"

    # Preview: both files present, nothing changed.
    plan = report(run(SCRIPT, *common))
    check(plan["state"] == "present" and plan["preview"] is True and plan["files"]["DBSIM.EXE"] == original["DBSIM.EXE"], plan)
    # Without the own chain -Apply refuses and leaves both files.
    refused(run(SCRIPT, *common, "-Apply", ok=False), "own chain is not deployed")
    check(all((install / n).exists() for n in RETIRED) and not record.exists())

    # The own chain from stand-in images.
    sources = {key: root / f"{key}.bin" for key in ("launcher", "shell", "sdl", "sim")}
    for key, path in sources.items():
        path.write_bytes(pe32(key))
    run(shell_script, *common, "-Apply", "-Launcher", sources["launcher"], "-Shell", sources["shell"], "-Sdl", sources["sdl"])
    run(sim_script, *common, "-Apply", "-Sim", sources["sim"], "-Sdl", sources["sdl"])

    # Apply: both files gone, the record lists them; the same again changes nothing.
    applied = report(run(SCRIPT, *common, "-Apply"))
    check(applied["state"] == "retired" and applied["changed"] is True, applied)
    check(not any((install / n).exists() for n in RETIRED), "files still present")
    entry = json.loads(record.read_text(encoding="utf-8"))
    check(entry["kind"] == "es2-retired-originals" and entry["files"] == {n: original[n] for n in RETIRED}, entry)
    check(report(run(SCRIPT, *common, "-Apply"))["changed"] is False)
    check(report(run(SCRIPT, *common))["state"] == "retired")
    for name in set(ORIGINALS) - set(RETIRED):
        check(sha(install / name) == original[name], name)

    # start-native: the own shell only, the retired files listed; the original shell refused.
    preview = report(run(start_native, *native, "-Preview"))
    check(preview["shell"] == "own" and sorted(preview["retired_originals"]) == sorted(RETIRED), preview)
    refused(run(start_native, *native, "-Preview", "-Shell", "vshell", ok=False), "only the own shell starts")
    # The own chain cannot be removed while the originals are retired.
    refused(run(shell_script, *common, "-Remove", ok=False), "VSHELL.EXE is retired")
    refused(run(sim_script, *common, "-Remove", ok=False), "DBSIM.EXE is retired")
    # A retired file back without -Restore: start-native and the script refuse.
    shutil.copyfile(extracted / "cd" / "VER95" / "DBSIM.EXE", install / "DBSIM.EXE")
    refused(run(start_native, *native, "-Preview", ok=False), "without retire-originals.ps1 -Restore")
    refused(run(SCRIPT, *common, ok=False), "Unexpected state")
    (install / "DBSIM.EXE").unlink()

    # Restore refuses a source without the recorded hash and changes nothing.
    vshell_source = extracted / "cd" / "VER95" / "VSHELL.EXE"
    kept = vshell_source.read_bytes()
    vshell_source.write_bytes(kept + b"x")
    refused(run(SCRIPT, *restore, ok=False), "does not have the recorded hash")
    check(record.exists() and not any((install / n).exists() for n in RETIRED))
    vshell_source.write_bytes(kept)

    # Restore: both back with their hashes, the record gone; start-native takes the original shell again.
    restored = report(run(SCRIPT, *restore))
    check(restored["state"] == "present" and restored["changed"] is True, restored)
    check(all(sha(install / n) == original[n] for n in RETIRED) and not record.exists())
    check(report(run(SCRIPT, *restore))["changed"] is False)
    check(report(run(start_native, *native, "-Preview", "-Shell", "vshell"))["shell"] == "vshell")

    # A changed original: the state is unknown, -Apply refuses.
    dbsim = install / "DBSIM.EXE"
    kept = dbsim.read_bytes()
    dbsim.write_bytes(kept + b"patched")
    refused(run(SCRIPT, *common, "-Apply", ok=False), "Unexpected state")
    dbsim.write_bytes(kept)

    # Another session's clone lock refuses -Apply (not the preview).
    locks = data / "installs" / ".clone-locks"
    locks.mkdir(parents=True, exist_ok=True)
    lock = locks / "played-mock.json"
    lock.write_text(json.dumps(dict(token="other", holder_pid=1, session="another")), encoding="utf-8")
    with open(lock, "rb"):
        refused(run(SCRIPT, *common, "-Apply", ok=False), "in use by another session")
        check(report(run(SCRIPT, *common))["state"] == "present")
    lock.unlink()

    # Refused installations and arguments: a measurement clone keeps its originals, native-r111 needs the owner's word.
    clone = data / "installs" / "timing-mock"
    shutil.copytree(install, clone)
    (clone / "installation-manifest.json").write_text(
        json.dumps(dict(manifest, destination=os.path.abspath(str(clone)), measurement_clone_of="X:\\source")), encoding="utf-8")
    refused(run(SCRIPT, "-DataRoot", data, "-InstallName", "timing-mock", "-Apply", ok=False), "measurement clone")
    check(all((clone / n).exists() for n in RETIRED))
    refused(run(SCRIPT, "-DataRoot", data, "-InstallName", "native-r111", ok=False), "owner's word")
    refused(run(SCRIPT, "-DataRoot", data, "-InstallName", "..\\x", ok=False), "plain installation name")
    refused(run(SCRIPT, *common, "-Apply", "-Restore", ok=False), "either -Apply or -Restore")
    refused(run(SCRIPT, "-DataRoot", data, "-InstallName", "nothing-here", ok=False), "Not a prepared installation")
    for name in ORIGINALS:
        check(sha(install / name) == original[name], name)
    SESSION.close()
    shutil.rmtree(root, ignore_errors=True)
    print(f"PASS retire_originals: {checks} checks (preview, no chain, apply, start-native, remove guards, stray file, "
          "restore source, restore, changed original, lock, installations)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
