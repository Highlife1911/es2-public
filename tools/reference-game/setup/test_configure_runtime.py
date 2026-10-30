"""configure-runtime.ps1 and the runtime check of start-native.ps1 on a
synthetic measurement clone.

The clone lives in a temporary DataRoot and holds stand-in files of our own
(no game file); its installation-manifest.json records their hashes as the
"originals". The deployed source is the own cw3220.dll (//rtl:cw3220.dll).
Nothing is launched; no real installation or clone is touched.
Usage: test_configure_runtime.py <configure-runtime.ps1> <cw3220.dll>
"""
import hashlib
import json
import os
import shutil
import sys
import tempfile
from pathlib import Path

SCRIPT = Path(sys.argv[1]).resolve()
OWN_DLL = Path(sys.argv[2]).resolve()
SETUP = SCRIPT.parent
PWSH = os.environ.get("ES2_PWSH") or shutil.which("pwsh")
ORIGINALS = ["ES.EXE", "VSHELL.EXE", "DBSIM.EXE", "CW3220.DLL", "BWCC32.DLL", "SOS9503.DLL", "SOSLIBS3.DLL"]
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


def run(script, *args, ok=True):
    # One PowerShell process for the test, a fresh runspace per step (pwsh_session.py).
    command = [str(script), *map(str, args)]
    result = SESSION.run(file=script, args=[str(a) for a in args], timeout=180)
    if (result.returncode == 0) != ok:
        print(json.dumps(dict(command=command, code=result.returncode, out=result.stdout, err=result.stderr)), flush=True)
    check((result.returncode == 0) == ok, (command, result.stdout, result.stderr))
    return result


def report(result):
    text = result.stdout
    return json.loads(text[text.index("{"):])


def main():
    if not PWSH:
        print("SKIP: no PowerShell 7 (ES2_PWSH)")
        return 0
    base = Path(os.environ.get("TEST_TMPDIR") or tempfile.gettempdir())
    root = Path(tempfile.mkdtemp(prefix="runtime-", dir=base))
    global SESSION
    SESSION = PwshSession(PWSH, scratch=root)
    data = root / "DataRoot"
    clone = data / "installs" / "own-runtime-mock"
    clone.mkdir(parents=True)
    extracted = root / "extracted"   # start-native.ps1 -ExtractedRoot (outside DataRoot)
    extracted.mkdir()
    (extracted / "earthsiege2-data.iso").write_bytes(b"stand-in, never mounted")
    original = {}
    for name in ORIGINALS:
        (clone / name).write_bytes(("stand-in for " + name + "\n").encode() * 50)
        original[name] = sha(clone / name)
    destination = os.path.abspath(str(clone))
    manifest = dict(destination=destination, measurement_clone_of=os.path.abspath(str(data / "installs" / "source")), language="German",
                    hashes=[dict(file=name, sha256=value) for name, value in original.items()])
    (clone / "installation-manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    common = ["-DataRoot", data, "-InstallName", "own-runtime-mock"]
    native = common + ["-ExtractedRoot", extracted]
    start_native = SETUP / "start-native.ps1"
    dll, kept, runtime = clone / "CW3220.DLL", clone / "CW3220-ORIGINAL.DLL", clone / "oracle-runtime-manifest.json"
    original_bytes = dll.read_bytes()

    # Preview: the state, nothing changed.
    plan = report(run(SCRIPT, *common))
    check(plan["state"] == "original" and plan["preview"] is True and plan["original_sha256"] == original["CW3220.DLL"], plan)
    check(not kept.exists() and not runtime.exists() and sha(dll) == original["CW3220.DLL"])
    preview = report(run(start_native, *native, "-Preview"))
    check(preview["original_hashes_verified"] is True and "runtime" not in preview, preview)

    # Apply: the original renamed, the own DLL in its place, both recorded.
    applied = report(run(SCRIPT, *common, "-Apply", "-Source", OWN_DLL))
    check(applied["applied"] is True and applied["state"] == "own" and applied["own_sha256"] == sha(OWN_DLL), applied)
    check(kept.read_bytes() == original_bytes and dll.read_bytes() == OWN_DLL.read_bytes() and not (clone / "CW3220.DLL.staged").exists())
    record = json.loads(runtime.read_text(encoding="utf-8"))
    check(record["own_sha256"] == sha(OWN_DLL) and record["original_sha256"] == original["CW3220.DLL"] and
          record["original_file"] == "CW3220-ORIGINAL.DLL" and record["delegate_mode"] == ["ES.EXE", "VSHELL.EXE"], record)
    preview = report(run(start_native, *native, "-Preview"))
    check(preview["runtime"] == "own" and preview["runtime_sha256"] == sha(OWN_DLL), preview)
    check(report(run(SCRIPT, *common, "-Apply", "-Source", OWN_DLL))["applied"] is False)

    # Refused sources: not the own DLL (a stand-in, the original itself).
    for source in (clone / "BWCC32.DLL", kept):
        result = run(SCRIPT, *common, "-Apply", "-Source", source, ok=False)
        check("Not the own cw3220.dll" in result.stderr + result.stdout, result.stderr)
    check(dll.read_bytes() == OWN_DLL.read_bytes() and kept.read_bytes() == original_bytes)

    # Changed files: start-native and both directions refuse; nothing is touched.
    for path, message in ((dll, "The own runtime changed"), (kept, "Original file changed: CW3220.DLL")):
        saved = path.read_bytes()
        path.write_bytes(saved + b"x")
        result = run(start_native, *native, "-Preview", ok=False)
        check(message in result.stderr + result.stdout, result.stderr)
        for action in ("-Remove", "-Apply"):
            result = run(SCRIPT, *common, action, "-Source", OWN_DLL, ok=False) if action == "-Apply" else run(SCRIPT, *common, action, ok=False)
            check("Unexpected runtime files" in result.stderr + result.stdout, result.stderr)
        check(path.read_bytes() == saved + b"x")
        path.write_bytes(saved)
    staged = clone / "CW3220.DLL.staged"
    staged.write_bytes(b"left over")
    other = root / "other.dll"
    other.write_bytes(OWN_DLL.read_bytes() + b"\0")   # the own DLL with one more byte: a new build
    result = run(SCRIPT, *common, "-Apply", "-Source", other, ok=False)
    check("interrupted run" in result.stderr + result.stdout, result.stderr)
    staged.unlink()

    # A new build replaces the deployed one; the original stays as it is.
    updated = report(run(SCRIPT, *common, "-Apply", "-Source", other))
    check(updated["applied"] is True and sha(dll) == sha(other) and kept.read_bytes() == original_bytes, updated)
    check(json.loads(runtime.read_text(encoding="utf-8"))["own_sha256"] == sha(other))

    # Remove: the original back under its name, the manifest gone.
    removed = report(run(SCRIPT, *common, "-Remove"))
    check(removed["removed"] is True and removed["state"] == "original", removed)
    check(dll.read_bytes() == original_bytes and not kept.exists() and not runtime.exists())
    check("runtime" not in report(run(start_native, *native, "-Preview")))
    check(report(run(SCRIPT, *common, "-Remove"))["removed"] is False)

    # Refused installations and arguments.
    for name, message in (("native-r111", "only on the owner's word"),("..\\x", "Only a named measurement clone")):
        result = run(SCRIPT, "-DataRoot", data, "-InstallName", name, ok=False)
        check(message in result.stderr + result.stdout, result.stderr)
    result = run(SCRIPT, *common, "-Apply", "-Remove", ok=False)
    check("either -Apply or -Remove" in result.stderr + result.stdout, result.stderr)
    plain = dict(manifest)
    del plain["measurement_clone_of"]
    (clone / "installation-manifest.json").write_text(json.dumps(plain), encoding="utf-8")
    result = run(SCRIPT, *common, "-Apply", "-Source", OWN_DLL, ok=False)
    check("separate measurement clone" in result.stderr + result.stdout, result.stderr)
    check(dll.read_bytes() == original_bytes and not kept.exists())
    for name in ORIGINALS:
        check(sha(clone / name) == original[name], name)

    # The primary installation only with -AllowPrimary and only with its own manifest
    # (destination itself, no measurement_clone_of); the preview touches nothing.
    primary = data / "installs" / "native-r111"
    primary.mkdir()
    for name in ORIGINALS:
        shutil.copyfile(clone / name, primary / name)
    own = dict(plain, destination=os.path.abspath(str(primary)))
    (primary / "installation-manifest.json").write_text(json.dumps(own), encoding="utf-8")
    plan = report(run(SCRIPT, "-DataRoot", data, "-InstallName", "native-r111", "-AllowPrimary"))
    check(plan["state"] == "original" and plan["preview"] is True, plan)
    (primary / "installation-manifest.json").write_text(json.dumps(dict(own, measurement_clone_of=destination)), encoding="utf-8")
    result = run(SCRIPT, "-DataRoot", data, "-InstallName", "native-r111", "-AllowPrimary", ok=False)
    check("Unexpected installation manifest" in result.stderr + result.stdout, result.stderr)
    check(not (primary / "CW3220-ORIGINAL.DLL").exists() and sha(primary / "CW3220.DLL") == original["CW3220.DLL"])
    SESSION.close()
    shutil.rmtree(root, ignore_errors=True)
    print(f"PASS configure_runtime: {checks} checks (preview, apply, update, remove, changed files, refused sources and installations)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
