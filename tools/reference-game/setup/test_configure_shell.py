"""configure-shell.ps1 and start-native.ps1's own-shell path on a
synthetic installation.

The installation lives in a temporary DataRoot and holds stand-in files of
our own (no game file); its installation-manifest.json records their hashes
as the "originals". The deployed sources are stand-in 32-bit PE images
written by the test. Nothing is launched (start-native only in -Preview); no
real installation is touched.
Usage: test_configure_shell.py <configure-shell.ps1>
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
    root = Path(tempfile.mkdtemp(prefix="ownshell-", dir=base))
    global SESSION
    SESSION = PwshSession(PWSH, scratch=root)
    data = root / "DataRoot"
    install = data / "installs" / "own-shell-mock"
    (install / "DATA").mkdir(parents=True)
    extracted = root / "extracted"   # start-native.ps1 -ExtractedRoot (outside DataRoot)
    extracted.mkdir()
    (extracted / "earthsiege2-data.iso").write_bytes(b"stand-in, never mounted")
    original = {}
    for name in ORIGINALS:
        (install / name).write_bytes(("stand-in for " + name + "\n").encode() * 50)
        original[name] = sha(install / name)
    destination = os.path.abspath(str(install))
    (install / "DATA" / "DRIVE.CFG").write_bytes(f"Q:\r\n{destination}\r\n".encode("ascii"))
    manifest = dict(destination=destination, language="German", hashes=[dict(file=n, sha256=v) for n, v in original.items()])
    (install / "installation-manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    sources = {}
    for key in ("launcher", "shell", "sdl"):
        sources[key] = root / f"{key}.bin"
        sources[key].write_bytes(pe32(key))
    src_args = ["-Launcher", sources["launcher"], "-Shell", sources["shell"], "-Sdl", sources["sdl"]]
    common = ["-DataRoot", data, "-InstallName", "own-shell-mock"]
    native = common + ["-ExtractedRoot", extracted]
    start_native = SETUP / "start-native.ps1"
    launcher, shell_dir = install / "ES2LAUNCH.EXE", install / "ES2SHELL"
    manifest_path = install / "own-shell-manifest.json"

    # Preview: nothing deployed, nothing changed; start-native defaults to the original path.
    plan = report(run(SCRIPT, *common))
    check(plan["state"] == "none" and plan["preview"] is True, plan)
    check(not launcher.exists() and not shell_dir.exists())
    preview = report(run(start_native, *native, "-Preview"))
    check(preview["shell"] == "vshell" and preview["program"].upper().endswith("ES.EXE"), preview)
    result = run(start_native, *native, "-Preview", "-Shell", "own", ok=False)
    check("not deployed" in result.stderr + result.stdout, result.stderr)

    # Apply: the three files, menu.ini from DRIVE.CFG, the manifest; start-native defaults to the own shell.
    applied = report(run(SCRIPT, *common, "-Apply", *src_args))
    check(applied["applied"] is True and applied["state"] == "own", applied)
    for key, name in (("launcher", launcher), ("shell", shell_dir / "es2shell.exe"), ("sdl", shell_dir / "SDL3.dll")):
        check(name.read_bytes() == sources[key].read_bytes() and applied["files"][key] == sha(sources[key]), key)
    ini = (shell_dir / "menu.ini").read_bytes().decode("ascii")
    check(ini.startswith("[Menu]\r\n") and "cd_root = Q:\r\n" in ini and f"game_dir = {destination}\r\n" in ini and "chain_dir = \r\n" in ini, ini)
    check("primary" not in ini, ini)   # only native-r111 gets primary = allowed
    record = json.loads(manifest_path.read_text(encoding="utf-8"))
    check(record["kind"] == "es2-own-shell" and record["files"] == applied["files"] and
          record["launch_shell"] == f'"{shell_dir / "es2shell.exe"}" --menu "{shell_dir}"', record)
    check(not (install / "ES2SHELL.staged").exists())
    preview = report(run(start_native, *native, "-Preview"))
    check(preview["shell"] == "own" and preview["program"].upper().endswith("ES2LAUNCH.EXE") and
          preview["launch_shell"] == record["launch_shell"] and preview["own_shell"]["shell"] == sha(sources["shell"]), preview)
    preview = report(run(start_native, *native, "-Preview", "-Shell", "vshell"))
    check(preview["shell"] == "vshell" and preview["program"].upper().endswith("ES.EXE"), preview)
    # The same build again: only menu.ini is refreshed (a new CD letter).
    (install / "DATA" / "DRIVE.CFG").write_bytes(f"R:\r\n{destination}\r\n".encode("ascii"))
    check(report(run(SCRIPT, *common, "-Apply", *src_args))["applied"] is False)
    check("cd_root = R:\r\n" in (shell_dir / "menu.ini").read_bytes().decode("ascii"))

    # Refused sources: not a PE32 image; nothing changed.
    bad = root / "bad.bin"
    bad.write_bytes(b"not an image" * 40)
    result = run(SCRIPT, *common, "-Apply", "-Launcher", bad, "-Shell", sources["shell"], "-Sdl", sources["sdl"], ok=False)
    check("Not a 32-bit PE image" in result.stderr + result.stdout, result.stderr)
    check(launcher.read_bytes() == sources["launcher"].read_bytes())

    # A changed deployed file: start-native refuses the own path, the script refuses to apply; -Remove clears it.
    saved = launcher.read_bytes()
    launcher.write_bytes(saved + b"x")
    result = run(start_native, *native, "-Preview", ok=False)
    check("The own shell changed" in result.stderr + result.stdout, result.stderr)
    result = run(SCRIPT, *common, "-Apply", *src_args, ok=False)
    check("Unexpected own-shell files" in result.stderr + result.stdout, result.stderr)
    launcher.write_bytes(saved)

    # A new build replaces the deployed one.
    newer = root / "launcher2.bin"
    newer.write_bytes(pe32("launcher-2"))
    updated = report(run(SCRIPT, *common, "-Apply", "-Launcher", newer, "-Shell", sources["shell"], "-Sdl", sources["sdl"]))
    check(updated["applied"] is True and launcher.read_bytes() == newer.read_bytes(), updated)
    check(json.loads(manifest_path.read_text(encoding="utf-8"))["files"]["launcher"] == sha(newer))

    # Remove: everything of the own shell gone, the originals untouched.
    (shell_dir / "shell.log").write_bytes(b"a session")
    removed = report(run(SCRIPT, *common, "-Remove"))
    check(removed["removed"] is True and removed["state"] == "none", removed)
    check(not launcher.exists() and not shell_dir.exists() and not manifest_path.exists())
    check(report(run(SCRIPT, *common, "-Remove"))["removed"] is False)
    check(report(run(start_native, *native, "-Preview"))["shell"] == "vshell")

    # Refused installations and arguments.
    result = run(SCRIPT, "-DataRoot", data, "-InstallName", "native-r111", ok=False)
    check("owner's word" in result.stderr + result.stdout, result.stderr)
    # With -AllowPrimary the shell's own refusal is lifted in menu.ini (primary = allowed).
    primary = data / "installs" / "native-r111"
    shutil.copytree(install, primary)
    (primary / "DATA" / "DRIVE.CFG").write_bytes(f"Q:\r\n{os.path.abspath(str(primary))}\r\n".encode("ascii"))
    (primary / "installation-manifest.json").write_text(json.dumps(dict(manifest, destination=os.path.abspath(str(primary)))), encoding="utf-8")
    applied = report(run(SCRIPT, "-DataRoot", data, "-InstallName", "native-r111", "-AllowPrimary", "-Apply", *src_args))
    ini = (primary / "ES2SHELL" / "menu.ini").read_bytes().decode("ascii")
    check(applied["applied"] is True and ini.endswith("chain_dir = \r\nprimary = allowed\r\n"), ini)
    shutil.rmtree(primary)
    result = run(SCRIPT, "-DataRoot", data, "-InstallName", "..\\x", ok=False)
    check("plain installation name" in result.stderr + result.stdout, result.stderr)
    result = run(SCRIPT, *common, "-Apply", "-Remove", ok=False)
    check("either -Apply or -Remove" in result.stderr + result.stdout, result.stderr)
    result = run(SCRIPT, "-DataRoot", data, "-InstallName", "nothing-here", ok=False)
    check("Not a prepared installation" in result.stderr + result.stdout, result.stderr)
    for name in ORIGINALS:
        check(sha(install / name) == original[name], name)
    SESSION.close()
    shutil.rmtree(root, ignore_errors=True)
    print(f"PASS configure_shell: {checks} checks (preview, apply, refresh, update, remove, changed files, refused sources and installations)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
