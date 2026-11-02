# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""The replay suites over the built site (docs/web_build.md section 16, "Sparse data").

usage: replay_site.py --build-site build_site.py --zstd-pack zstd_pack.exe --codec vfs.js
                      [replay_suite.py options] --sim-argv NODE host.mjs es2sim.js

replay_suite.py with es2sim's wasm build under web/node/host.mjs over the site's file system
instead of an installation clone: unfetched bytes of a mapping are poisoned (--poison), the
installation is the site's C:\\ES2 with language G (--language G). The site is built from the
CD below ES2_DATA_ROOT into TEST_TMPDIR, or taken from ES2_REPLAY_SITE when that directory holds
a manifest.json.

The working directory of a run holds only what replay_suite.prepare_workdir writes over the CD's
files: the four launch files in DATA, the PREFS override and the demo list. host.mjs stages it
into C:\\ES2 and copies the files the run changed back (--stage), where collect_outputs reads
results.dat and mission.var. The INI, the tick log and the journal stay on node:fs (--host-dir
for the output directory and the registry's directory).
"""
import argparse
import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("replay_suite", os.path.join(HERE, "replay_suite.py"))
rs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rs)

LANGUAGE = "G"


def site_prepare(install, cd, launch_dir, plan, prefs_override=None):
    """What prepare_workdir writes over the CD's DATA, SAV, TAPES and VOL; the site holds the rest."""
    os.makedirs(os.path.join(install, "DATA"))
    for name in rs.LAUNCH_FILES:
        shutil.copyfile(os.path.join(launch_dir, name), os.path.join(install, "DATA", name))
    if prefs_override:
        shutil.copyfile(prefs_override, os.path.join(install, "DATA", "PREFS.CFG"))
    if plan.get("demo_tape"):
        os.makedirs(os.path.join(install, "TAPES"))
        with open(os.path.join(install, "TAPES", "DEMOLIST.STR"), "wb") as stream:
            stream.write(rs.demo_list(plan["demo_tape"]))


def build_site(build_site_py, zstd_pack, cd, scratch):
    given = os.environ.get("ES2_REPLAY_SITE")
    if given and os.path.isfile(os.path.join(given, "manifest.json")):
        return os.path.abspath(given)
    out = os.path.join(scratch, "replay-site")
    command = [sys.executable, "-B", build_site_py, "--cd", cd, "--out", out, "--zstd-pack", zstd_pack]
    if os.path.isdir(out):
        command.append("--clean")
    completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if completed.returncode != 0:
        sys.stdout.write(completed.stdout.decode("utf-8", "replace"))
        raise SystemExit("replay_site: the site build failed")
    return os.path.abspath(out)


def main(argv=None):
    argv, sim_argv = rs.split_sim_argv(sys.argv[1:] if argv is None else argv)
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], add_help=False)
    parser.add_argument("--build-site", required=True)
    parser.add_argument("--zstd-pack", required=True)
    parser.add_argument("--codec", required=True)
    parser.add_argument("--output-dir")
    parser.add_argument("--registry")
    ours, rest = parser.parse_known_args(argv)
    if not sim_argv or len(sim_argv) < 3:
        parser.error("--sim-argv NODE host.mjs es2sim.js is required")
    registry = os.path.abspath(ours.registry or rs.default_paths()[0])
    scratch = os.environ.get("TEST_TMPDIR") or tempfile.gettempdir()
    output_dir = os.path.abspath(ours.output_dir or os.environ.get("TEST_UNDECLARED_OUTPUTS_DIR")
                                 or os.path.join(scratch, "replay-results"))
    cd = rs.find_cd(os.environ.get("ES2_DATA_ROOT", ""))
    if cd and "--list" not in rest:
        site = build_site(os.path.abspath(ours.build_site), os.path.abspath(ours.zstd_pack), cd, scratch)
        options = ["--site", site, "--codec", os.path.abspath(ours.codec), "--poison", "--language", LANGUAGE,
                   "--stage", ".", "--host-dir", output_dir, "--host-dir", os.path.dirname(registry)]
        sim_argv = sim_argv[:-1] + options + sim_argv[-1:]
        rs.prepare_workdir = site_prepare
    return rs.main(rest + ["--registry", registry, "--output-dir", output_dir, "--sim-argv"] + sim_argv)


if __name__ == "__main__":
    sys.exit(main())
